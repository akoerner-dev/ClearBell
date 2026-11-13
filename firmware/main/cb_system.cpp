// cb_system.cpp - see cb_system.h.
#include "cb_system.h"

#include <Arduino.h>
#include <Preferences.h>
#include <esp_app_desc.h>

#include "cb_build_stamp.h"  // generated at every build (main/build_stamp.cmake, FW-B-11)
#include <esp_core_dump.h>
#include <esp_heap_caps.h>
#include <esp_mac.h>
#include <esp_ota_ops.h>
#include <esp_system.h>

#include "cb_config.h"
#include "cb_console.h"
#include "cb_crypto.h"
#include "cb_log.h"
#include "cb_modes.h"
#include "cb_nvs_secure.h"
#include "cb_provisioning.h"
#include "cb_status.h"
#include "cb_update_sig.h"

namespace {

uint32_t boot_count = 0;
esp_reset_reason_t reset_reason = ESP_RST_UNKNOWN;
bool selftest_ok = false;
bool selftest_run = false;
bool update_pending = false;
bool coredump_cached = false;  // for the status page: reading the dump header every 5 s is not needed

const char *reason_text(esp_reset_reason_t r) {
  switch (r) {
    case ESP_RST_POWERON: return "power-on";
    case ESP_RST_EXT: return "external pin";
    case ESP_RST_SW: return "software (reboot command / update)";
    case ESP_RST_PANIC: return "PANIC (crash, see core dump)";
    case ESP_RST_INT_WDT: return "INTERRUPT WATCHDOG";
    case ESP_RST_TASK_WDT: return "TASK WATCHDOG (something hung)";
    case ESP_RST_WDT: return "OTHER WATCHDOG";
    case ESP_RST_DEEPSLEEP: return "deep sleep";
    case ESP_RST_BROWNOUT: return "BROWNOUT (supply voltage dipped)";
    case ESP_RST_SDIO: return "SDIO";
    case ESP_RST_USB: return "USB (reset from the PC)";
    case ESP_RST_JTAG: return "JTAG";
    default: return "unknown";
  }
}

bool coredump_present() {
  size_t addr = 0, size = 0;
  return esp_core_dump_image_get(&addr, &size) == ESP_OK && size > 0;
}

void run_selftest() {
  int failed = cb::hmac_selftest();
  cb_log("selftest: HMAC-SHA256 RFC 4231 %s (%d failed)", failed == 0 ? "OK" : "FAILED", failed);
  uint32_t t0 = millis();
  int sig_failed = cb::sig_selftest();  // update signatures (R26): RSA-3072/PSS against an espsecure vector
  cb_log("selftest: update signature %s (%d failed, %lu ms)", sig_failed == 0 ? "OK" : "FAILED", sig_failed,
         (unsigned long)(millis() - t0));
  selftest_ok = failed == 0 && sig_failed == 0;
  selftest_run = true;
}

void cmd_info(const char *) {
  const esp_app_desc_t *app = esp_app_get_description();
  const esp_partition_t *run = esp_ota_get_running_partition();
  uint8_t mac[6];
  esp_read_mac(mac, ESP_MAC_WIFI_STA);
  cb_log("ClearBell firmware %s, image '%s', built %s, IDF %s", app->version, CB_UNIT_TYPE_NAME, CB_BUILD_STAMP,
         app->idf_ver);
  cb_log("MAC %02X:%02X:%02X:%02X:%02X:%02X, partition %s, update pending %s", mac[0], mac[1], mac[2], mac[3],
         mac[4], mac[5], run ? run->label : "?", update_pending ? "YES" : "no");
  cb_log("boot #%lu, last reset: %s, uptime %lu s", (unsigned long)boot_count, reason_text(reset_reason),
         (unsigned long)(millis() / 1000));
  cb_log("heap free %lu B (min %lu B), PSRAM free %lu B, log lines dropped %lu",
         (unsigned long)esp_get_free_heap_size(), (unsigned long)esp_get_minimum_free_heap_size(),
         (unsigned long)heap_caps_get_free_size(MALLOC_CAP_SPIRAM), (unsigned long)cb_log_dropped());
  cb_log("core dump stored: %s, selftest %s", coredump_present() ? "YES (crash-test or real crash)" : "no",
         !selftest_run ? "not run yet" : selftest_ok ? "OK" : "FAILED");
}

void cmd_selftest(const char *) {
  run_selftest();
}

void cmd_reboot(const char *) {
  cb_log("rebooting");
  delay(50);
  esp_restart();
}

// Deliberately hangs loop() for 10 s. Expected: the task watchdog fires after 5 s and reboots; the next
// 'info' shows "last reset: TASK WATCHDOG". Proves the watchdog is armed (Softwarearchitektur 3.2).
void cmd_wdt_test(const char *) {
  cb_log("wdt-test: blocking loop() for 10 s - expect a watchdog reboot after %d s", CB_WDT_TIMEOUT_S);
  uint32_t t0 = millis();
  while (millis() - t0 < 10000) {
    // busy wait without yielding
  }
  cb_log("wdt-test: FAILED - still alive after 10 s, the watchdog is NOT armed");
}

// Deliberately crashes. Expected: reboot, "last reset: PANIC", "core dump stored: YES".
void cmd_crash_test(const char *) {
  cb_log("crash-test: calling abort() - expect reboot with core dump");
  delay(50);
  abort();
}

void cmd_coredump_clear(const char *) {
  cb_log("core dump %s", esp_core_dump_image_erase() == ESP_OK ? "erased" : "erase failed");
  coredump_cached = coredump_present();
}

}  // namespace

void cb_system_begin() {
  reset_reason = esp_reset_reason();

  Preferences p;
  if (p.begin("cb_sys", false)) {
    boot_count = p.getUInt("boots", 0) + 1;
    p.putUInt("boots", boot_count);
    p.end();
  }

  esp_ota_img_states_t state;
  const esp_partition_t *run = esp_ota_get_running_partition();
  update_pending = run && esp_ota_get_state_partition(run, &state) == ESP_OK && state == ESP_OTA_IMG_PENDING_VERIFY;

  cb_console_add("info", "version, reset reason, boot counter, memory, core dump", cmd_info);
  cb_console_add("selftest", "run the crypto known-answer tests (HMAC RFC 4231, update signature)", cmd_selftest);
  cb_console_add("reboot", "restart the unit", cmd_reboot);
  cb_console_add("wdt-test", "hang loop() 10 s: the watchdog must reboot the unit", cmd_wdt_test);
  cb_console_add("crash-test", "abort(): unit must reboot and keep a core dump", cmd_crash_test);
  cb_console_add("coredump-clear", "erase the stored core dump", cmd_coredump_clear);

  coredump_cached = coredump_present();
  cmd_info(nullptr);
  run_selftest();
}

void cb_system_poll() {
  // Confirming or rolling back an update moved to cb_update (port step 10, health checks incl. WiFi and peers).
  // update_pending here only reflects the state at boot, for 'info' and the status page.
  esp_ota_img_states_t st;
  const esp_partition_t *run = esp_ota_get_running_partition();
  static uint32_t next = 0;
  if (update_pending && (int32_t)(millis() - next) >= 0) {
    next = millis() + 5000;
    update_pending = run && esp_ota_get_state_partition(run, &st) == ESP_OK && st == ESP_OTA_IMG_PENDING_VERIFY;
  }
}

void cb_system_selftest(bool *run, bool *ok) {
  *run = selftest_run;
  *ok = selftest_ok;
}

uint32_t cb_system_boot_count() {
  return boot_count;
}

const char *cb_system_reset_reason() {
  return reason_text(reset_reason);
}

void cb_system_json(cb::Json &j) {
  const esp_app_desc_t *app = esp_app_get_description();
  const esp_partition_t *run = esp_ota_get_running_partition();
  j.key("system");
  j.begin_obj();
  j.kv("version", app->version);
  j.kv("image", CB_UNIT_TYPE_NAME);
  j.kv("name", cb_prov().valid ? cb_prov().name : "");
  j.kv("role", cb_prov().valid ? cb::unit_name(cb_prov().role) : "none");
  // FW-B-21: what the buyer calls the flats, by key (EG = key at the bottom, OG = top). Outdoor: both; indoor: own.
  j.key("flat_names");
  j.begin_obj();
  for (cb::UnitId f : {cb::UnitId::EG, cb::UnitId::OG})
    if (CB_IS_OUTDOOR || cb_prov().role == f) j.kv(cb::unit_name(f), cb_prov_flat_name(f));
  j.end_obj();
  j.kv("mode", cb::mode_name(cb_mode()));
  cb_nvs_secure_json(j);  // Härtung Stufe B: storage encrypted?, eFuse security version
  j.kv("built", CB_BUILD_STAMP);
  j.kv("uptime_s", (unsigned long)(millis() / 1000));
  j.kv("boot", (unsigned long)boot_count);
  j.kv("reset_reason", reason_text(reset_reason));
  // Internal RAM is what runs out first (stacks, WiFi buffers, DMA); PSRAM separately.
  j.kv("heap_free", (unsigned long)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
  j.kv("heap_min", (unsigned long)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL));
  j.kv("psram_free", (unsigned long)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
  j.kv("ota_slot", run ? run->label : "?");
  j.kv("update_pending", update_pending);
  j.kv("coredump", coredump_cached);
  j.kv("selftest", !selftest_run ? "not run" : selftest_ok ? "ok" : "FAILED");
  j.kv("log_dropped", (unsigned long)cb_log_dropped());
  bool wall;
  uint32_t t = cb_status_now(&wall);
  if (wall) {
    time_t tt = (time_t)t;
    struct tm tm;
    localtime_r(&tt, &tm);
    char buf[24];
    strftime(buf, sizeof buf, "%Y-%m-%d %H:%M:%S", &tm);
    j.kv("time", buf);
  } else {
    j.key("time");
    j.null();  // NTP has not set the clock (no internet yet)
  }
  j.end_obj();
}
