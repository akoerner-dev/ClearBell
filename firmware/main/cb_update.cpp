// cb_update.cpp - see cb_update.h.
#include "cb_update.h"

#include <Arduino.h>
#include <Preferences.h>
#include <esp_app_desc.h>
#include <esp_ota_ops.h>
#include <esp_system.h>

#include <atomic>
#include <cstring>

#include "cb_config.h"
#include "cb_console.h"
#include "cb_log.h"
#include "cb_net.h"
#include "cb_status.h"
#include "cb_system.h"
#include "cb_update_logic.h"

namespace {

enum Fault : uint8_t { kFaultOff = 0, kFaultCrash = 1, kFaultNoWifi = 2 };

bool probation = false;       // running image is PENDING_VERIFY
uint8_t peers_before = 0;     // remembered when the update was installed
bool skip_wifi = false;
const char *verdict_done = nullptr;

// Upload state (web task only)
esp_ota_handle_t ota = 0;
const esp_partition_t *target = nullptr;
uint8_t head[cb::kImageHeaderNeed];
size_t head_len = 0, written = 0, expected = 0;
bool started = false;
std::atomic<uint32_t> reboot_at{0};
char last_result[96] = "-";

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
  cb_console_add("update-fault", "TEST: update-fault crash|nowifi|off - let the next update fail", cmd_update_fault);
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

bool cb_update_start(size_t total, char *msg, size_t n) {
  cb_update_abort();
  target = esp_ota_get_next_update_partition(nullptr);
  if (!target) {
    snprintf(msg, n, "kein freier Firmware-Bereich");
    return false;
  }
  if (total < cb::kImageHeaderNeed || total > target->size) {
    snprintf(msg, n, "Dateigröße passt nicht (max. %lu KB)", (unsigned long)(target->size / 1024));
    return false;
  }
  if (probation) {
    snprintf(msg, n, "die laufende Firmware ist noch nicht bestätigt - bitte einige Minuten warten");
    return false;
  }
  expected = total;
  head_len = written = 0;
  started = false;
  return true;
}

bool cb_update_write(const uint8_t *d, size_t len, char *msg, size_t n) {
  if (!target) return false;
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
    esp_err_t e = esp_ota_begin(target, OTA_WITH_SEQUENTIAL_WRITES, &ota);
    if (e != ESP_OK) {
      snprintf(msg, n, "Update nicht startbar (%s)", esp_err_to_name(e));
      return false;
    }
    started = true;
    cb_log("update: receiving %s (%u B) into %s", version, (unsigned)expected, target->label);
    if (esp_ota_write(ota, head, head_len) != ESP_OK) {
      snprintf(msg, n, "Schreibfehler");
      return false;
    }
    written = head_len;
  }
  if (len && esp_ota_write(ota, d, len) != ESP_OK) {
    snprintf(msg, n, "Schreibfehler");
    return false;
  }
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
  snprintf(msg, n, "Firmware %s installiert - Neustart, danach Probezeit bis zu 5 min", desc.version);
  snprintf(last_result, sizeof last_result, "%s installed", desc.version);
  cb_log("update: %s", msg);
  reboot_at = millis() + 2000;  // time for the HTTP answer to leave
  return true;
}

void cb_update_abort() {
  if (started && ota) esp_ota_abort(ota);
  started = false;
  ota = 0;
  target = nullptr;
}

void cb_update_json(cb::Json &j) {
  j.key("update");
  j.begin_obj();
  j.kv("probation", probation);
  j.kv("last", last_result);
  j.end_obj();
}
