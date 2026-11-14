// cb_status.cpp - see cb_status.h.
#include "cb_status.h"

#include <Arduino.h>
#include <LittleFS.h>
#include <Preferences.h>
#include <esp_http_server.h>
#include <esp_timer.h>
#include <lwip/sockets.h>

#include <atomic>
#include <cstring>
#include <ctime>

#include "cb_audio.h"
#include "cb_config.h"
#include "cb_console.h"
#include "cb_crypto.h"
#include "cb_door.h"
#include "cb_http_auth.h"
#include "cb_json.h"
#include "cb_led.h"
#include "cb_log.h"
#include "cb_setup_logic.h"
#include "cb_modes.h"
#include "cb_net.h"
#include "cb_http_util.h"
#include "cb_provisioning.h"
#include "cb_remote.h"
#include "cb_status_page.h"
#include "cb_system.h"
#include "cb_touch.h"
#include "cb_update.h"
#include "cb_setup.h"
#include "cb_setup_page.h"
#include "cb_pairing.h"
#include "cb_phones.h"

namespace {

constexpr const char *kUser = "clearbell";
constexpr size_t kMaxUpload = 1024 * 1024;  // 1 MB = 11 s mono at 44.1 kHz; the ring tone has 0.3 MB
constexpr size_t kFsReserve = 32 * 1024;    // keep this much of LittleFS free
constexpr const char *kTmpFile = "/upload.tmp";
constexpr uint32_t kWallClockValid = 1700000000;  // 2023-11: anything earlier means "NTP has not set it"

// ---- Door log (SA 6.4) -------------------------------------------------------------------------------------
cb::DoorLog dlog;
bool dlog_loaded = false;

void dlog_save() {
  uint8_t blob[cb::DoorLog::kBlobSize];
  dlog.serialize(blob);
  Preferences p;
  if (p.begin("cb_dlog", false)) {
    if (p.putBytes("log", blob, sizeof blob) != sizeof blob) cb_log("status: door log NOT saved (NVS full?)");
    p.end();
  }
}

void dlog_load() {
  uint8_t blob[cb::DoorLog::kBlobSize];
  Preferences p;
  size_t n = 0;
  if (p.begin("cb_dlog", true)) {
    n = p.getBytesLength("log") == sizeof blob ? p.getBytes("log", blob, sizeof blob) : 0;
    p.end();
  }
  dlog_loaded = true;
  if (n && !dlog.load(blob, n)) cb_log("status: door log damaged or old format - starting empty");
}

void fmt_time(const cb::DoorLogEntry &e, char *out, size_t n) {
  if (e.wall) {
    time_t t = (time_t)e.time;
    struct tm tm;
    localtime_r(&t, &tm);
    strftime(out, n, "%Y-%m-%d %H:%M:%S", &tm);
  } else {
    snprintf(out, n, "Start #%u + %lu s", (unsigned)e.boot, (unsigned long)e.time);
  }
}

void dlog_json(cb::Json &j) {
  j.key("doorlog");
  j.begin_obj();
#if CB_IS_OUTDOOR
  // SA 6.4: openings by the indoor keys only as a sum - no time, no flat.
  j.kv("indoor_key_openings", (unsigned long)dlog.local_count());
  j.kv("scope", "outdoor: remote and service openings one by one, indoor-key openings only as sum");
#else
  j.kv("scope", "own flat only");
#endif
  j.key("entries");
  j.begin_arr();
  for (int i = 0; i < dlog.size(); i++) {
    cb::DoorLogEntry e = dlog.newest(i);
    char t[32];
    fmt_time(e, t, sizeof t);
    j.begin_obj();
    j.kv("time", t);
    j.kv("event", cb::door_event_name(e.event));
    j.end_obj();
  }
  j.end_arr();
  j.kv("total", (unsigned long)dlog.total_added());
  j.end_obj();
}

void cmd_doorlog(const char *args) {
  if (!strcmp(args, "clear JA")) {
    dlog.clear();
    dlog_save();
    cb_log("doorlog: cleared");
    return;
  }
#if CB_IS_OUTDOOR
  cb_log("doorlog: openings by indoor keys (sum only): %lu", (unsigned long)dlog.local_count());
#endif
  cb_log("doorlog: %d entries (of %lu ever), newest first:", dlog.size(), (unsigned long)dlog.total_added());
  for (int i = 0; i < dlog.size(); i++) {
    cb::DoorLogEntry e = dlog.newest(i);
    char t[32];
    fmt_time(e, t, sizeof t);
    cb_log("  %-22s %s", t, cb::door_event_name(e.event));
  }
}

// ---- Snapshot: the web task asks, loop() builds ------------------------------------------------------------
SemaphoreHandle_t snap_req = nullptr, snap_done = nullptr;
char snap_buf[8192];
size_t snap_len = 0;
bool snap_ok = false;
uint32_t snap_ms_max = 0;

void build_snapshot() {
  uint32_t t0 = micros();
  cb::Json j(snap_buf, sizeof snap_buf);
  j.begin_obj();
  cb_system_json(j);
  if (cb_prov().valid) cb_net_json(j);
  cb_touch_json(j);
  cb_door_json(j);
  cb_remote_json(j);
  cb_pairing_json(j);
  cb_phones_json(j);
  dlog_json(j);
  cb_audio_json(j);
  cb_update_json(j);
  // Settings card of the page: what can be changed there, and under which condition.
  j.key("settings");
  j.begin_obj();
  j.kv("volume", (unsigned)cb_audio_volume());
  j.kv("led_green_percent", (unsigned)cb_led_green_percent());
  j.kv("own_password", cb_prov_has_own_web_pass());
  j.end_obj();
  j.end_obj();
  snap_ok = j.ok();
  snap_len = j.len();
  uint32_t dt = (micros() - t0) / 1000;
  if (dt > snap_ms_max) snap_ms_max = dt;
}

// ---- Web server --------------------------------------------------------------------------------------------
httpd_handle_t server = nullptr;
cb::DigestAuth auth;
char www_auth[200];  // must outlive httpd_resp_send
uint32_t req_count = 0, upload_ok = 0, upload_failed = 0;
std::atomic<bool> maintenance_activity{false};  // web task -> loop: restart the 10-min window

uint32_t now_s() {
  return (uint32_t)(esp_timer_get_time() / 1000000);
}

void peer_ip(httpd_req_t *req, char *out, size_t n) {
  struct sockaddr_in6 a = {};
  socklen_t len = sizeof a;
  out[0] = 0;
  if (getpeername(httpd_req_to_sockfd(req), (struct sockaddr *)&a, &len) == 0) {
    if (a.sin6_family == AF_INET6) inet_ntop(AF_INET, &a.sin6_addr.un.u32_addr[3], out, n);  // v4-mapped
    else inet_ntop(AF_INET, &((struct sockaddr_in *)&a)->sin_addr, out, n);
  }
}

bool authorize(httpd_req_t *req) {
  req_count++;
  // RV-39: a writing request from a foreign page is refused BEFORE any login shortcut (also over the access point).
  if (!cb_http_origin_ok(req, cb_setup_from_ap(req))) {
    char ip[16];
    peer_ip(req, ip, sizeof ip);
    cb_log("web: %s %s from %s refused - not from this unit's own page (cross-site / missing X-ClearBell)",
           http_method_str((enum http_method)req->method), req->uri, ip);
    httpd_resp_set_status(req, "403 Forbidden");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"ok\":false,\"message\":\"Anfrage von fremder Seite abgelehnt\"}");
    return false;
  }
  // Over the setup access point the WPA2 password (= device code) already proved possession (cb_setup.h).
  if (cb_setup_from_ap(req)) return true;
  char hdr[512];
  const char *h = nullptr;
  size_t l = httpd_req_get_hdr_value_len(req, "Authorization");
  if (l && l < sizeof hdr && httpd_req_get_hdr_value_str(req, "Authorization", hdr, sizeof hdr) == ESP_OK) h = hdr;
  const char *method = http_method_str((enum http_method)req->method);  // GET, PUT, POST ...
  cb::AuthResult r = auth.check(h, method, req->uri, now_s());
  if (r == cb::AuthResult::Ok) return true;
  if (r != cb::AuthResult::Missing && r != cb::AuthResult::Stale) {
    char ip[16];
    peer_ip(req, ip, sizeof ip);
    cb_log("web: login %s from %s (%s %s)%s", cb::auth_result_name(r), ip, method, req->uri,
           auth.locked(now_s()) ? " - LOCKED for 30 s" : "");
  }
  httpd_resp_set_hdr(req, "Connection", "close");  // the body of a refused upload is not read
  if (r == cb::AuthResult::LockedOut) {
    httpd_resp_set_status(req, "429 Too Many Requests");
    httpd_resp_sendstr(req, "Zu viele falsche Passwoerter - 30 s warten.\n");
    return false;
  }
  auth.challenge(now_s(), r == cb::AuthResult::Stale, www_auth, sizeof www_auth);
  httpd_resp_set_status(req, "401 Unauthorized");
  httpd_resp_set_hdr(req, "WWW-Authenticate", www_auth);
  httpd_resp_sendstr(req, "Anmeldung noetig: Benutzer clearbell, Geraetepasswort der Einheit.\n");
  return false;
}

void security_headers(httpd_req_t *req) {
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");
  httpd_resp_set_hdr(req, "X-Content-Type-Options", "nosniff");
  httpd_resp_set_hdr(req, "X-Frame-Options", "DENY");
  // Only our own inline script and same-origin fetches; nothing from outside.
  httpd_resp_set_hdr(req, "Content-Security-Policy",
                     "default-src 'none'; script-src 'unsafe-inline'; style-src 'unsafe-inline'; connect-src 'self'");
}

esp_err_t h_page(httpd_req_t *req) {
  if (!authorize(req)) return ESP_OK;
  security_headers(req);
  httpd_resp_set_type(req, "text/html; charset=utf-8");
  // Setup page in setup mode, over the access point, or while not set up; otherwise the status page.
  bool setup = cb_mode() == cb::Mode::Setup || cb_setup_from_ap(req) || !cb_prov().valid;
  return httpd_resp_send(req, setup ? kSetupPage : kStatusPage, HTTPD_RESP_USE_STRLEN);
}

esp_err_t h_status(httpd_req_t *req) {
  if (!authorize(req)) return ESP_OK;
  security_headers(req);
  xSemaphoreTake(snap_done, 0);  // drop a late answer to an earlier, timed-out request
  xSemaphoreGive(snap_req);
  if (xSemaphoreTake(snap_done, pdMS_TO_TICKS(1000)) != pdTRUE) {
    httpd_resp_set_status(req, "503 Service Unavailable");
    return httpd_resp_sendstr(req, "{\"error\":\"main loop did not answer\"}");
  }
  httpd_resp_set_type(req, "application/json");
  if (!snap_ok) {
    httpd_resp_set_status(req, "500 Internal Server Error");
    return httpd_resp_sendstr(req, "{\"error\":\"snapshot too large\"}");
  }
  return httpd_resp_send(req, snap_buf, snap_len);
}

esp_err_t reply_upload(httpd_req_t *req, const char *status, const char *msg) {
  char body[200];
  cb::Json j(body, sizeof body);
  j.begin_obj();
  j.kv("ok", status[0] == '2');
  j.kv("message", msg);
  j.end_obj();
  httpd_resp_set_status(req, status);
  httpd_resp_set_type(req, "application/json");
  if (status[0] != '2') {
    upload_failed++;
    cb_log("web: tone upload %s refused: %s", req->uri, msg);
  }
  return httpd_resp_send(req, body, j.len());
}

// PUT /ton/<name>: body = the WAV file. Written to a temp file, checked as 16-bit PCM WAV, then renamed into
// place while no tone plays (SA 6.3) - an aborted upload never leaves half a ring tone.
esp_err_t h_upload(httpd_req_t *req) {
  if (!authorize(req)) return ESP_OK;
  security_headers(req);
  cb::Access acc = cb::mode_access(cb::Endpoint::ToneUpload, cb_access_input(req));  // RV-45: rules in cb_access
  if (!acc.ok) return reply_upload(req, acc.status, acc.message);
  maintenance_activity = true;  // using the page in maintenance keeps maintenance open
  bool known = false;
  cb::Sound s = cb_audio_parse_sound(req->uri + strlen("/ton/"), &known);
  if (!known) return reply_upload(req, "404 Not Found", "unbekannter Ton (ring, ok, error, nonet, tick)");
  size_t len = req->content_len;
  if (len < 44 || len > kMaxUpload) return reply_upload(req, "413 Payload Too Large", "WAV 44 B ... 1 MB");
  if (!cb_audio_fs_ok()) return reply_upload(req, "500 Internal Server Error", "Dateisystem nicht eingehaengt");
  size_t free_b = LittleFS.totalBytes() - LittleFS.usedBytes();
  if (len + kFsReserve > free_b) return reply_upload(req, "507 Insufficient Storage", "kein Platz im Dateisystem");

  File f = LittleFS.open(kTmpFile, "w");
  if (!f) return reply_upload(req, "500 Internal Server Error", "Zwischendatei nicht anlegbar");
  static uint8_t chunk[1024];
  size_t got = 0;
  bool ok = true;
  cb::BodyReader rd = cb_http_reader(req, 30000 + len / 10);  // RV-17: 30 s + 10 kB/s, two 10-s stalls end it
  while (got < len) {
    int n = rd.read((char *)chunk, len - got > sizeof chunk ? sizeof chunk : len - got);
    if (n <= 0 || f.write(chunk, n) != (size_t)n) {
      ok = false;
      break;
    }
    got += n;
  }
  f.close();
  if (!ok) {
    LittleFS.remove(kTmpFile);
    return reply_upload(req, "400 Bad Request", "Upload abgebrochen - alter Ton bleibt");
  }
  char msg[96];
  if (!cb_audio_install(kTmpFile, s, msg, sizeof msg)) {
    LittleFS.remove(kTmpFile);
    return reply_upload(req, "422 Unprocessable Entity", msg);
  }
  upload_ok++;
  return reply_upload(req, "200 OK", msg);
}

// PUT /update[?older=1]: body = the SIGNED firmware image (cb.py signieren / release page). Maintenance only.
// older=1 allows a lower version than the running one (box on the page). Details in cb_update.h.
esp_err_t h_update(httpd_req_t *req) {
  if (!authorize(req)) return ESP_OK;
  security_headers(req);
  cb::Access acc = cb::mode_access(cb::Endpoint::Update, cb_access_input(req));  // RV-45: rules in cb_access
  if (!acc.ok) return reply_upload(req, acc.status, acc.message);
  maintenance_activity = true;
  char msg[160], q[32], v[4];
  bool older = httpd_req_get_url_query_str(req, q, sizeof q) == ESP_OK &&
               httpd_query_key_value(q, "older", v, sizeof v) == ESP_OK && v[0] == '1';
  if (!cb_update_start(req->content_len, older, msg, sizeof msg)) return reply_upload(req, "422 Unprocessable Entity", msg);
  static uint8_t chunk[2048];
  size_t got = 0, len = req->content_len;
  bool ok = true;
  msg[0] = 0;
  cb::BodyReader rd = cb_http_reader(req, 30000 + len / 10);  // RV-17
  while (got < len) {
    int n = rd.read((char *)chunk, len - got > sizeof chunk ? sizeof chunk : len - got);
    if (n <= 0) {
      snprintf(msg, sizeof msg, "Upload abgebrochen - alte Firmware bleibt");
      ok = false;
      break;
    }
    if (!cb_update_write(chunk, n, msg, sizeof msg)) {
      ok = false;
      break;
    }
    got += n;
  }
  if (ok) ok = cb_update_finish(msg, sizeof msg);
  if (!ok) {
    cb_update_abort();
    return reply_upload(req, "422 Unprocessable Entity", msg);
  }
  upload_ok++;
  return reply_upload(req, "200 OK", msg);
}

// ---- Settings (status page card "Einstellungen") ------------------------------------------------------------
// POST /settings, form fields (each optional): vol=15|30|50|75|100 (any mode - the boot button changes it without a
// login anyway, Meister 08.11.2026), led=5..100 (maintenance only, like every other change), tone=1 (test tone at
// the current volume), ownerkey / owneronly (maintenance only, R26). The answer is the same JSON shape as the
// uploads: {ok, message}.
esp_err_t reply_settings(httpd_req_t *req, const char *status, const char *msg) {
  char body[160];
  cb::Json j(body, sizeof body);
  j.begin_obj();
  j.kv("ok", status[0] == '2');
  j.kv("message", msg);
  j.end_obj();
  httpd_resp_set_status(req, status);
  httpd_resp_set_type(req, "application/json");
  if (status[0] != '2') cb_log("web: setting refused: %s", msg);
  return httpd_resp_send(req, body, j.len());
}

esp_err_t h_settings(httpd_req_t *req) {
  if (!authorize(req)) return ESP_OK;
  security_headers(req);
  char body[192], v[8];
  if (req->content_len == 0 || req->content_len >= sizeof body) return reply_settings(req, "400 Bad Request", "Wert fehlt");
  if (!cb_http_read_body(req, body, sizeof body)) return reply_settings(req, "400 Bad Request", "Anfrage abgebrochen");
  char msg[96];
  msg[0] = 0;
  if (cb::form_get(body, "led", v, sizeof v)) {
    cb::Access acc = cb::mode_access(cb::Endpoint::SettingsLed, cb_access_input(req));
    if (!acc.ok) return reply_settings(req, acc.status, acc.message);
    maintenance_activity = true;
    int x = atoi(v);  // range check on the int: a cast to uint8_t would turn 261 into 5
    if (x < 5 || x > 100 || !cb_led_set_green_percent((uint8_t)x)) return reply_settings(req, "400 Bad Request", "LED-Abgleich 5 bis 100 %");
    snprintf(msg, sizeof msg, "LED-Abgleich %d %% gespeichert", atoi(v));
  }
  if (cb::form_get(body, "vol", v, sizeof v)) {
    int x = atoi(v);
    if (x < 15 || x > 100 || !cb_audio_set_volume((uint8_t)x))
      return reply_settings(req, "400 Bad Request", "Lautstärke 15, 30, 50, 75 oder 100 %");
    snprintf(msg, sizeof msg, "Lautstärke %d %% gespeichert", atoi(v));
  }
  if (cb::form_get(body, "tone", v, sizeof v)) {
    cb_audio_play(cb::Sound::Success);
    if (!msg[0]) snprintf(msg, sizeof msg, "Probeton bei %u %%", (unsigned)cb_audio_volume());
  }
  // Own update key (R26): ownerkey=<64 hex> | "-" (remove); owneronly=1|0. Maintenance only.
  char key[72];
  bool has_key = cb::form_get(body, "ownerkey", key, sizeof key), has_only = cb::form_get(body, "owneronly", v, sizeof v);
  if (has_key || has_only) {
    cb::Access acc = cb::mode_access(cb::Endpoint::SettingsOwnerKey, cb_access_input(req));
    if (!acc.ok) return reply_settings(req, acc.status, acc.message);
    maintenance_activity = true;
    if (has_key && !cb_update_set_owner_key(key, msg, sizeof msg)) return reply_settings(req, "400 Bad Request", msg);
    if (has_only && !cb_update_set_owner_only(v[0] == '1', msg, sizeof msg))
      return reply_settings(req, "400 Bad Request", msg);
  }
  if (!msg[0]) return reply_settings(req, "400 Bad Request", "nichts zu ändern");
  return reply_settings(req, "200 OK", msg);
}

// ---- Own password (cb_http_auth.h: password_change_open) ----------------------------------------------------
// GET /settings/pw-nonce -> {"nonce": 32 hex}, one at a time, valid 5 min, used once.
// POST /settings/password  enc=<64 hex>&mac=<32 hex>  - maintenance only. 5 wrong current passwords -> 60 s lock.
portMUX_TYPE pw_lock = portMUX_INITIALIZER_UNLOCKED;
char pw_nonce[33] = "";
uint32_t pw_nonce_at = 0, pw_wrong = 0, pw_locked_until = 0;

esp_err_t h_pw_nonce(httpd_req_t *req) {
  if (!authorize(req)) return ESP_OK;
  security_headers(req);
  uint8_t r[16];
  cb::random_bytes(r, sizeof r);
  char n[33];
  for (int i = 0; i < 16; i++) snprintf(n + 2 * i, 3, "%02x", r[i]);
  taskENTER_CRITICAL(&pw_lock);
  memcpy(pw_nonce, n, sizeof n);
  pw_nonce_at = now_s();
  taskEXIT_CRITICAL(&pw_lock);
  char body[64];
  snprintf(body, sizeof body, "{\"nonce\":\"%s\"}", n);
  httpd_resp_set_type(req, "application/json");
  return httpd_resp_sendstr(req, body);
}

esp_err_t h_password(httpd_req_t *req) {
  if (!authorize(req)) return ESP_OK;
  security_headers(req);
  cb::Access acc = cb::mode_access(cb::Endpoint::PasswordChange, cb_access_input(req));
  if (!acc.ok) return reply_settings(req, acc.status, acc.message);
  maintenance_activity = true;
  if (pw_locked_until && (int32_t)(pw_locked_until - now_s()) > 0)
    return reply_settings(req, "429 Too Many Requests", "zu oft falsches aktuelles Passwort - 1 min warten");
  char body[160], enc[70], mac[40];
  if (req->content_len == 0 || req->content_len >= sizeof body)
    return reply_settings(req, "400 Bad Request", "Anfrage unvollständig");
  if (!cb_http_read_body(req, body, sizeof body)) return reply_settings(req, "400 Bad Request", "Anfrage abgebrochen");
  if (!cb::form_get(body, "enc", enc, sizeof enc) || !cb::form_get(body, "mac", mac, sizeof mac))
    return reply_settings(req, "400 Bad Request", "Anfrage unvollständig");
  char nonce[33];
  taskENTER_CRITICAL(&pw_lock);
  bool fresh = pw_nonce[0] && now_s() - pw_nonce_at < 300;
  memcpy(nonce, pw_nonce, sizeof nonce);
  pw_nonce[0] = 0;  // used once, whatever the outcome
  taskEXIT_CRITICAL(&pw_lock);
  if (!fresh) return reply_settings(req, "409 Conflict", "Sitzung abgelaufen - Seite neu laden und nochmal");
  char pw[cb::kPasswordMax + 1];
  bool wrong = false;
  if (!cb::password_change_open(cb_prov().dev_pass, nonce, enc, mac, pw, sizeof pw, &wrong)) {
    if (wrong && ++pw_wrong >= 5) {
      pw_wrong = 0;
      pw_locked_until = now_s() + 60;
    }
    return reply_settings(req, "400 Bad Request",
                          wrong ? "aktuelles Passwort falsch" : "neues Passwort: 4 bis 32 Zeichen, ohne Umlaute");
  }
  pw_wrong = 0;
  if (!cb_prov_set_web_pass(pw)) return reply_settings(req, "500 Internal Server Error", "nicht gespeichert");
  auth.set_password(cb_prov().dev_pass);
  memset(pw, 0, sizeof pw);
  cb_log("web: status-page password %s", cb_prov_has_own_web_pass() ? "changed (own)" : "back to the label");
  return reply_settings(req, "200 OK",
                        cb_prov_has_own_web_pass() ? "Passwort geändert - bitte gleich neu anmelden"
                                                   : "Etikett-Passwort gilt wieder - bitte gleich neu anmelden");
}

// Console (USB = physical access): web-pass <new> / web-pass label
void cmd_web_pass(const char *args) {
  const char *pw = !strcmp(args, "label") ? "" : args;
  if (!*args || (*pw && !cb::password_allowed(pw))) {
    cb_log("usage: web-pass <4..32 printable ASCII characters> | web-pass label   (now: %s)",
           cb_prov_has_own_web_pass() ? "own password" : "label password");
    return;
  }
  if (!cb_prov_set_web_pass(pw)) {
    cb_log("web-pass: NOT saved");
    return;
  }
  auth.set_password(cb_prov().dev_pass);
  cb_log("web-pass: %s", *pw ? "own password set" : "back to the label password");
}

void start_server() {
  // Per-boot secret for the login nonces: a reboot makes every old nonce worthless.
  uint8_t secret[32];
  cb::random_bytes(secret, sizeof secret);
  char realm[48];
  snprintf(realm, sizeof realm, "ClearBell %s", cb_prov().name);
  auth.begin(secret, realm, kUser, cb_prov().dev_pass);
  memset(secret, 0, sizeof secret);

  httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
  cfg.max_open_sockets = 3;      // a phone and a PC at the same time are plenty; each socket costs RAM
  cfg.lru_purge_enable = true;   // a stuck client does not lock others out
  cfg.stack_size = 8192;
  cfg.task_priority = 2;         // below door (5), audio (3) and loop-related work: the page comes last (SA 8.1)
  cfg.core_id = 0;
  cfg.recv_wait_timeout = 10;
  cfg.send_wait_timeout = 10;
  cfg.uri_match_fn = httpd_uri_match_wildcard;
  cfg.max_uri_handlers = 24;   // outdoor 16, indoor 17 registered (08.11.2026)
  if (httpd_start(&server, &cfg) != ESP_OK) {
    cb_log("status: web server NOT started");
    server = nullptr;
    return;
  }
  httpd_uri_t page = {"/", HTTP_GET, h_page, nullptr};
  httpd_uri_t status = {"/status.json", HTTP_GET, h_status, nullptr};
  httpd_uri_t upload = {"/ton/*", HTTP_PUT, h_upload, nullptr};
  httpd_uri_t update = {"/update", HTTP_PUT, h_update, nullptr};
  httpd_uri_t settings = {"/settings", HTTP_POST, h_settings, nullptr};
  httpd_register_uri_handler(server, &page);
  httpd_register_uri_handler(server, &settings);
  httpd_uri_t pwn = {"/settings/pw-nonce", HTTP_GET, h_pw_nonce, nullptr};
  httpd_uri_t pwc = {"/settings/password", HTTP_POST, h_password, nullptr};
  httpd_register_uri_handler(server, &pwn);
  httpd_register_uri_handler(server, &pwc);
  httpd_register_uri_handler(server, &status);
  httpd_register_uri_handler(server, &upload);
  httpd_register_uri_handler(server, &update);
  cb_setup_register(server);
  cb_pairing_register(server);
  cb_phones_register(server);
  cb_setup_register_catchall(server);  // last: matches everything else (captive portal)
  cb_log("status: web server on port 80, user '%s', realm '%s'", kUser, realm);
}

void cmd_web(const char *) {
  cb_log("web: %s, requests %lu, logins ok %lu, wrong %lu, locks %lu, uploads ok %lu / refused %lu, "
         "slowest snapshot %lu ms (%u of %u B)",
         server ? "running" : "NOT running", (unsigned long)req_count, (unsigned long)auth.ok_count(),
         (unsigned long)auth.fail_count(), (unsigned long)auth.lock_count(), (unsigned long)upload_ok,
         (unsigned long)upload_failed, (unsigned long)snap_ms_max, (unsigned)snap_len, (unsigned)sizeof snap_buf);
}

}  // namespace

void cb_status_begin() {
  dlog_load();
  snap_req = xSemaphoreCreateBinary();
  snap_done = xSemaphoreCreateBinary();
  cb_console_add("doorlog", "door log of this unit (doorlog clear JA = erase)", cmd_doorlog);
  cb_console_add("web", "status page: server state and login counters", cmd_web);
  cb_console_add("web-pass", "web-pass <new> | label - own status-page password", cmd_web_pass);
  // Runs also while not set up: it serves the setup page over the access point (cb_setup).
  if (cb_prov().factory_ok) start_server();
  else cb_log("status: no device code (factory step missing) - web server off");
}

void cb_status_poll() {
  if (maintenance_activity.exchange(false)) cb_modes_activity();
  if (snap_req && xSemaphoreTake(snap_req, 0) == pdTRUE) {
    build_snapshot();
    xSemaphoreGive(snap_done);
  }
}

bool cb_status_authorize(httpd_req_t *req) {
  return authorize(req);
}

bool cb_status_web_running() {
  return server != nullptr;
}

uint32_t cb_status_now(bool *wall) {
  time_t t = time(nullptr);
  *wall = t >= (time_t)kWallClockValid;
  return *wall ? (uint32_t)t : (uint32_t)(millis() / 1000);
}

void cb_status_door_event(cb::DoorEvent e) {
  if (!dlog_loaded) return;
  bool wall;
  uint32_t t = cb_status_now(&wall);
  dlog.add(e, t, wall, (uint16_t)cb_system_boot_count());
  dlog_save();
}

void cb_status_door_local() {
  if (!dlog_loaded) return;
  dlog.count_local();
  dlog_save();
}
