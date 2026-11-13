// cb_update.cpp - see cb_update.h.
#include "cb_update.h"

#include <Arduino.h>
#include <Preferences.h>
#include <esp_app_desc.h>
#include <esp_efuse.h>
#include <esp_image_format.h>
#include <esp_ota_ops.h>
#include <esp_system.h>
#include <mbedtls/sha256.h>

#include <atomic>
#include <cstring>

#include "cb_config.h"
#include "cb_console.h"
#include "cb_log.h"
#include "cb_net.h"
#include "cb_status.h"
#include "cb_system.h"
#include "cb_update_logic.h"
#include "cb_update_sig.h"

namespace {

enum Fault : uint8_t { kFaultOff = 0, kFaultCrash = 1, kFaultNoWifi = 2 };

bool probation = false;       // running image is PENDING_VERIFY
uint8_t peers_before = 0;     // remembered when the update was installed
bool skip_wifi = false;

// Upload state (web task only)
esp_ota_handle_t ota = 0;
const esp_partition_t *target = nullptr;
uint8_t head[cb::kImageHeaderNeed];
size_t head_len = 0, written = 0, expected = 0;
bool started = false;
bool allow_older = false;
// Signature (step 10d): bytes [0, sig_off) are hashed, [sig_off, expected) is the signature sector.
size_t sig_off = 0, consumed = 0;
mbedtls_sha256_context sha;
bool sha_on = false;
uint8_t sig_sector[cb::kSigSectorLen];
std::atomic<uint32_t> reboot_at{0};
char last_result[96] = "-";

constexpr const char *kSetNs = "cb_set";  // buyer settings: the factory reset clears this namespace

// ---- Which keys this unit trusts ----------------------------------------------------------------------------------
cb::TrustedKeys trust() {
  cb::TrustedKeys t = cb::maker_trust();
  Preferences p;
  if (p.begin(kSetNs, true)) {
    // isKey() first: getBytes() on a missing key logs an error line - every 5 s while the status page is open.
    t.has_owner = p.isKey("upd_owner") && p.getBytes("upd_owner", t.owner, sizeof t.owner) == sizeof t.owner;
    t.owner_only = t.has_owner && p.getUChar("upd_ownonly", 0) == 1;
    p.end();
  }
  return t;
}

const char *sig_message(cb::SigCheck c) {
  switch (c) {
    case cb::SigCheck::NoSignature: return "Datei ist nicht signiert - bitte die signierte Datei verwenden";
    case cb::SigCheck::BadBlock: return "Signatur beschädigt oder unbekanntes Verfahren";
    case cb::SigCheck::OtherFile: return "Datei wurde nach dem Signieren verändert";
    case cb::SigCheck::UnknownKey: return "signiert, aber mit einem Schlüssel, dem diese Einheit nicht vertraut";
    case cb::SigCheck::BadSignature: return "Signatur ungültig";
    case cb::SigCheck::Ok: return "Signatur gültig";
  }
  return "?";
}

void consume(const uint8_t *d, size_t len) {  // in upload order: hash the image part, keep the signature sector
  while (len) {
    if (consumed < sig_off) {
      size_t k = sig_off - consumed < len ? sig_off - consumed : len;
      mbedtls_sha256_update(&sha, d, k);
      d += k, len -= k, consumed += k;
    } else {
      size_t at = consumed - sig_off;
      size_t k = sizeof sig_sector - at < len ? sizeof sig_sector - at : len;
      memcpy(sig_sector + at, d, k);
      d += k, len -= k, consumed += k;
    }
  }
}

uint8_t peers_mask() {
  uint8_t m = 0;
  for (int i = 1; i < 4; i++)
    if (cb_net_peer_reachable((cb::UnitId)i)) m |= (uint8_t)(1u << i);
  return m;
}

void cmd_update_status(const char *) {
  const esp_partition_t *run = esp_ota_get_running_partition();
  cb_log("update: running %s from %s, %s; last upload: %s", esp_app_get_description()->version, run ? run->label : "?",
         probation ? "ON PROBATION" : "confirmed", last_result);
  if (probation)
    cb_log("update: needs self-test, WiFi, status page and peers 0x%02x (now 0x%02x); up %lu s of max %lu s",
           peers_before, peers_mask(), (unsigned long)(millis() / 1000),
           (unsigned long)(cb::kProbationMaxMs / 1000));
}

void log_digest(const char *what, const uint8_t d[32], bool active) {
  char hex[65];
  for (int i = 0; i < 32; i++) snprintf(hex + 2 * i, 3, "%02x", d[i]);
  cb_log("update-keys: %-11s %s%s", what, hex, active ? "" : "  (NOT accepted: owner only)");
}

void cmd_update_keys(const char *) {
  cb::TrustedKeys t = trust();
  log_digest("main key", t.main, !t.owner_only);
  log_digest("spare key", t.spare, !t.owner_only);
  if (t.has_owner) log_digest("owner key", t.owner, true);
  else cb_log("update-keys: owner key   none");
}

void cmd_update_owner(const char *args) {
  char msg[120];
  if (!*args) {
    cb_log("usage: update-owner <64 hex digits> | update-owner clear");
    return;
  }
  cb_update_set_owner_key(strcmp(args, "clear") ? args : "", msg, sizeof msg);
  cb_log("update-owner: %s", msg);
}

void cmd_update_owner_only(const char *args) {
  char msg[120];
  if (strcmp(args, "on") && strcmp(args, "off")) {
    cb_log("usage: update-owner-only on|off  - on: manufacturer keys no longer accepted (needs an owner key)");
    return;
  }
  cb_update_set_owner_only(!strcmp(args, "on"), msg, sizeof msg);
  cb_log("update-owner-only: %s", msg);
}

void cmd_update_fault(const char *args) {
  uint8_t f = !strcmp(args, "crash") ? kFaultCrash : !strcmp(args, "nowifi") ? kFaultNoWifi : kFaultOff;
  if (f == kFaultOff && strcmp(args, "off") != 0) {
    cb_log("usage: update-fault crash|nowifi|off  - TEST: the next update fails on probation and must roll back");
    return;
  }
  Preferences p;
  if (p.begin("cb_sys", false)) {
    p.putUChar("upd_fault", f);
    p.end();
  }
  cb_log("update-fault: %s (acts only while the NEXT update is on probation)", args);
}

}  // namespace

void cb_update_begin() {
  const esp_partition_t *run = esp_ota_get_running_partition();
  esp_ota_img_states_t st;
  probation = run && esp_ota_get_state_partition(run, &st) == ESP_OK && st == ESP_OTA_IMG_PENDING_VERIFY;
  Preferences p;
  uint8_t fault = kFaultOff;
  if (p.begin("cb_sys", false)) {
    peers_before = p.getUChar("upd_peers", 0);
    fault = p.getUChar("upd_fault", kFaultOff);
    if (probation && fault != kFaultOff) p.putUChar("upd_fault", kFaultOff);  // one shot
    p.end();
  }
  cb_console_add("update-status", "firmware slot, probation state, last upload", cmd_update_status);
  cb_console_add("update-keys", "keys this unit trusts for updates (R26)", cmd_update_keys);
  cb_console_add("update-owner", "update-owner <64 hex>|clear - own update key (owner)", cmd_update_owner);
  cb_console_add("update-owner-only", "update-owner-only on|off - accept only the owner key", cmd_update_owner_only);
  cb_console_add("update-fault", "TEST: update-fault crash|nowifi|off - let the next update fail", cmd_update_fault);
  cb::TrustedKeys t = trust();
  cb_log("update: signed updates only; trusted: main + spare manufacturer key%s%s", t.has_owner ? " + owner key" : "",
         t.owner_only ? " - OWNER ONLY (manufacturer keys off)" : "");
  if (!probation) return;
  cb_log("update: NEW FIRMWARE %s on probation - confirmed when healthy, else back to the old one",
         esp_app_get_description()->version);
  if (fault == kFaultCrash) {
    cb_log("update: TEST FAULT 'crash' - aborting, the bootloader must return to the old firmware");
    delay(100);
    abort();
  }
  skip_wifi = fault == kFaultNoWifi;
  if (skip_wifi) cb_log("update: TEST FAULT 'nowifi' - WiFi stays off, rollback expected after 5 min");
}

bool cb_update_skip_wifi() {
  return skip_wifi;
}

void cb_update_poll() {
  uint32_t r = reboot_at.load();
  if (r && (int32_t)(millis() - r) >= 0) {
    cb_log("update: rebooting into the new firmware");
    delay(50);
    esp_restart();
  }
  if (!probation) return;
  static uint32_t next = 0;
  if ((int32_t)(millis() - next) < 0) return;
  next = millis() + 1000;
  cb::UpdateHealth h{};
  h.uptime_ms = millis();
  cb_system_selftest(&h.selftest_run, &h.selftest_ok);
  h.wifi_up = cb_net_wifi_up();
  h.web_up = cb_status_web_running();
  h.peers_before = peers_before;
  h.peers_now = peers_mask();
  switch (cb::update_verdict(h)) {
    case cb::UpdateVerdict::Wait: break;
    case cb::UpdateVerdict::Confirm:
      probation = false;
      cb_log("update: firmware %s CONFIRMED after %lu s (%s)", esp_app_get_description()->version,
             (unsigned long)(millis() / 1000),
             esp_ota_mark_app_valid_cancel_rollback() == ESP_OK ? "old slot released" : "mark FAILED");
      break;
    case cb::UpdateVerdict::Rollback:
      cb_log("update: firmware NOT healthy (self-test %s, WiFi %s, web %s, peers 0x%02x of 0x%02x) - ROLLBACK",
             h.selftest_ok ? "ok" : "bad", h.wifi_up ? "up" : "down", h.web_up ? "up" : "down", h.peers_now,
             h.peers_before);
      delay(100);
      esp_ota_mark_app_invalid_rollback_and_reboot();  // does not return when an old image exists
      probation = false;
      break;
  }
}

bool cb_update_start(size_t total, bool older_ok, char *msg, size_t n) {
  cb_update_abort();
  target = esp_ota_get_next_update_partition(nullptr);
  if (!target) {
    snprintf(msg, n, "kein freier Firmware-Bereich");
    return false;
  }
  if (total < cb::kImageHeaderNeed + cb::kSigSectorLen || total > target->size) {
    snprintf(msg, n, "Dateigröße passt nicht (max. %lu KB)", (unsigned long)(target->size / 1024));
    return false;
  }
  if (total % cb::kSigSectorLen != 0) {  // espsecure pads to 4 KB and appends 4 KB - a plain build output never fits
    snprintf(msg, n, "%s", sig_message(cb::SigCheck::NoSignature));
    return false;
  }
  if (probation) {
    snprintf(msg, n, "die laufende Firmware ist noch nicht bestätigt - bitte einige Minuten warten");
    return false;
  }
  expected = total;
  sig_off = total - cb::kSigSectorLen;
  head_len = written = consumed = 0;
  started = false;
  allow_older = older_ok;
  memset(sig_sector, 0, sizeof sig_sector);
  mbedtls_sha256_init(&sha);
  sha_on = mbedtls_sha256_starts(&sha, 0) == 0;
  if (!sha_on) {
    snprintf(msg, n, "Prüfsumme nicht startbar");
    return false;
  }
  return true;
}

bool cb_update_write(const uint8_t *d, size_t len, char *msg, size_t n) {
  if (!target || !sha_on) return false;
  if (!started) {
    size_t k = sizeof head - head_len < len ? sizeof head - head_len : len;
    memcpy(head + head_len, d, k);
    head_len += k;
    d += k;
    len -= k;
    if (head_len < sizeof head) return true;
    char version[33];
    cb::ImageCheck c = cb::check_image_header(head, head_len, CB_UNIT_TYPE_NAME, version);
    if (c != cb::ImageCheck::Ok) {
      snprintf(msg, n, "keine passende Firmware: %s%s%s", cb::image_check_name(c), version[0] ? " - " : "", version);
      return false;
    }
    const char *running = esp_app_get_description()->version;
    if (!allow_older && cb::version_older(version, running)) {
      snprintf(msg, n, "ältere Version %s (läuft: %s) - nur mit „ältere Version zulassen“", version, running);
      return false;
    }
    // Härtung Stufe B (B2): below the security version in the eFuse nothing boots any more - say so before writing.
    uint32_t sv = cb::image_secure_version(head), fuse = esp_efuse_read_secure_version();
    if (!cb::secure_version_allowed(sv, fuse)) {
      snprintf(msg, n, "Sicherheitsstufe %lu dieser Firmware liegt unter der des Geräts (%lu) - startet hier nie mehr",
               (unsigned long)sv, (unsigned long)fuse);
      return false;
    }
    esp_err_t e = esp_ota_begin(target, OTA_WITH_SEQUENTIAL_WRITES, &ota);
    if (e != ESP_OK) {
      snprintf(msg, n, "Update nicht startbar (%s)", esp_err_to_name(e));
      return false;
    }
    started = true;
    cb_log("update: receiving %s (%u B) into %s%s", version, (unsigned)expected, target->label,
           allow_older ? ", older version allowed" : "");
    if (esp_ota_write(ota, head, head_len) != ESP_OK) {
      snprintf(msg, n, "Schreibfehler");
      return false;
    }
    consume(head, head_len);
    written = head_len;
  }
  if (len && esp_ota_write(ota, d, len) != ESP_OK) {
    snprintf(msg, n, "Schreibfehler");
    return false;
  }
  consume(d, len);
  written += len;
  return true;
}

bool cb_update_finish(char *msg, size_t n) {
  if (!started || written != expected) {
    snprintf(msg, n, "unvollständig (%u von %u B)", (unsigned)written, (unsigned)expected);
    return false;
  }
  esp_err_t e = esp_ota_end(ota);  // checks the appended SHA-256 and the image structure
  started = false;
  ota = 0;
  if (e != ESP_OK) {
    snprintf(msg, n, "Firmware beschädigt (%s) - alte Firmware bleibt", esp_err_to_name(e));
    return false;
  }
  // The bootable image must lie completely inside the signed part - nothing executable in the signature sector.
  esp_image_metadata_t meta = {};
  esp_partition_pos_t pos = {target->address, target->size};
  if (esp_image_verify(ESP_IMAGE_VERIFY_SILENT, &pos, &meta) != ESP_OK || meta.image_len > sig_off) {
    snprintf(msg, n, "Firmware passt nicht zur Signatur (Länge) - alte Firmware bleibt");
    cb_log("update: REFUSED - image length %u, signed part %u", (unsigned)meta.image_len, (unsigned)sig_off);
    return false;
  }
  uint8_t digest[32];
  bool hashed = mbedtls_sha256_finish(&sha, digest) == 0;
  mbedtls_sha256_free(&sha);
  sha_on = false;
  cb::SigResult sr{};
  if (hashed) sr = cb::check_signature(sig_sector, digest, trust());
  if (!hashed || sr.check != cb::SigCheck::Ok) {
    snprintf(msg, n, "nicht installiert: %s - alte Firmware bleibt", hashed ? sig_message(sr.check) : "Prüfsumme");
    snprintf(last_result, sizeof last_result, "refused: %s", hashed ? cb::sig_check_name(sr.check) : "hash");
    cb_log("update: REFUSED - %s (%d signature block(s))", hashed ? cb::sig_check_name(sr.check) : "hash failed",
           sr.blocks);
    return false;
  }
  esp_app_desc_t desc;
  esp_ota_get_partition_description(target, &desc);
  Preferences p;
  if (p.begin("cb_sys", false)) {
    p.putUChar("upd_peers", peers_mask());  // these must answer again before the new firmware is kept
    p.end();
  }
  e = esp_ota_set_boot_partition(target);
  if (e != ESP_OK) {
    snprintf(msg, n, "Umschalten fehlgeschlagen (%s)", esp_err_to_name(e));
    return false;
  }
  snprintf(msg, n, "Firmware %s installiert (signiert: %s) - Neustart, danach Probezeit bis zu 5 min", desc.version,
           sr.key == cb::SigKey::Owner ? "eigener Schlüssel" : sr.key == cb::SigKey::Spare ? "Ersatzschlüssel" : "Hersteller");
  snprintf(last_result, sizeof last_result, "%s installed (%s)", desc.version, cb::sig_key_name(sr.key));
  cb_log("update: %s", msg);
  reboot_at = millis() + 2000;  // time for the HTTP answer to leave
  return true;
}

void cb_update_abort() {
  if (started && ota) esp_ota_abort(ota);
  if (sha_on) mbedtls_sha256_free(&sha);
  sha_on = false;
  started = false;
  ota = 0;
  target = nullptr;
}

bool cb_update_set_owner_key(const char *hex, char *msg, size_t n) {
  bool remove = !hex || !*hex || !strcmp(hex, "-");
  uint8_t d[cb::kKeyDigestLen];
  if (!remove && !cb::parse_key_digest(hex, d)) {
    snprintf(msg, n, "Schlüssel-Kennung: genau 64 Hex-Zeichen (0-9, a-f)");
    return false;
  }
  cb::TrustedKeys t = cb::maker_trust();
  if (!remove && (!memcmp(d, t.main, sizeof d) || !memcmp(d, t.spare, sizeof d))) {
    snprintf(msg, n, "das ist ein Herstellerschlüssel - der gilt ohnehin");
    return false;
  }
  Preferences p;
  if (!p.begin(kSetNs, false)) {
    snprintf(msg, n, "Speicher nicht verfügbar");
    return false;
  }
  bool ok;
  if (remove) {
    p.remove("upd_owner");
    p.remove("upd_ownonly");
    ok = true;
  } else {
    ok = p.putBytes("upd_owner", d, sizeof d) == sizeof d;
  }
  p.end();
  char fp[17] = "";
  if (!remove) cb::key_fingerprint(d, fp);
  snprintf(msg, n, !ok ? "nicht gespeichert" : remove ? "eigener Update-Schlüssel entfernt - es gelten die Herstellerschlüssel"
                                                      : "eigener Update-Schlüssel %s gespeichert", fp);
  cb_log("update: owner key %s", !ok ? "NOT saved" : remove ? "removed" : fp);
  return ok;
}

bool cb_update_set_owner_only(bool on, char *msg, size_t n) {
  if (on && !trust().has_owner) {
    snprintf(msg, n, "erst einen eigenen Update-Schlüssel eintragen");
    return false;
  }
  Preferences p;
  bool ok = p.begin(kSetNs, false) && p.putUChar("upd_ownonly", on ? 1 : 0) == 1;
  p.end();
  snprintf(msg, n, !ok ? "nicht gespeichert"
                   : on ? "nur noch Updates mit dem eigenen Schlüssel - Herstellerschlüssel abgeschaltet"
                        : "Herstellerschlüssel werden wieder angenommen");
  cb_log("update: owner only %s", !ok ? "NOT saved" : on ? "ON" : "off");
  return ok;
}

void cb_update_json(cb::Json &j) {
  cb::TrustedKeys t = trust();
  char fp[17];
  j.key("update");
  j.begin_obj();
  j.kv("probation", probation);
  j.kv("last", last_result);
  j.key("keys");
  j.begin_obj();
  cb::key_fingerprint(t.main, fp);
  j.kv("main", fp);
  cb::key_fingerprint(t.spare, fp);
  j.kv("spare", fp);
  if (t.has_owner) {
    cb::key_fingerprint(t.owner, fp);
    j.kv("owner", fp);
  }
  j.kv("owner_only", t.owner_only);
  j.end_obj();
  j.end_obj();
}
