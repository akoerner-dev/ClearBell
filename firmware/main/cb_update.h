// cb_update.h - firmware updates by upload on the status page (port step 10).
//
// Buyer's view: download "clearbell-<version>-<aussen|innen>.bin" from the release page, put the unit into
// maintenance (boot button 5 s), open the status page, "Firmware aktualisieren", choose the file. No PC tool.
// (No signing key for now - decision 06.11.2026, docs/Produktreife.md; protection = physical presence for
// maintenance + device password + image checks + rollback.)
//
// What happens:
//   1. The first ~300 bytes are checked (cb::check_image_header): ESP32-S3, project "clearbell", this unit type.
//   2. The rest is streamed into the PASSIVE app slot (sequential erase, the running firmware is untouched).
//   3. esp_ota_end() verifies the SHA-256 the build appended; only then the slot becomes the boot slot, and the
//      peers reachable right now are remembered. Reboot.
//   4. The new firmware runs on probation (cb::update_verdict): confirmed after >= 60 s when self-test, WiFi,
//      status page and the previously reachable peers are fine; otherwise back to the old firmware after 5 min.
//      A crash or watchdog reset during probation makes the bootloader roll back by itself.
//
// Console (USB, tests): update-status, update-fault crash|nowifi|off (makes the NEXT probation fail on purpose,
// to prove the rollback; only ever acts while an update is on probation, then clears itself).
#pragma once

#include <cstddef>
#include <cstdint>

#include "cb_json.h"

void cb_update_begin();   // early in setup(): probation state; executes an armed test fault
void cb_update_poll();    // from loop(): confirm or roll back a firmware on probation
bool cb_update_skip_wifi();  // test fault "nowifi" armed for this probation boot

// Web task, in this order. Each returns false with a German message for the page; then call cb_update_abort().
bool cb_update_start(size_t total, char *msg, size_t n);
bool cb_update_write(const uint8_t *data, size_t len, char *msg, size_t n);
bool cb_update_finish(char *msg, size_t n);  // success: reboot follows in ~2 s
void cb_update_abort();

void cb_update_json(cb::Json &j);
