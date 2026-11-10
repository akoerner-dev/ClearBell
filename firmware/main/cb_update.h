// cb_update.h - firmware updates by upload on the status page (port step 10, signed since step 10d / R26).
//
// Buyer's view: download "clearbell-<version>-<aussen|innen>.bin" from the release page, put the unit into
// maintenance (boot button 5 s), open the status page, "Firmware aktualisieren", choose the file. No PC tool.
// Protection: physical presence for maintenance + device password + SIGNATURE (cb_update_sig.h) + image checks +
// downgrade protection + rollback.
//
// What happens:
//   1. The upload must be a multiple of 4096 bytes: image, padding, 4096-byte signature sector (cb.py signieren).
//   2. The first ~300 bytes are checked (cb::check_image_header): ESP32-S3, project "clearbell", this unit type, and
//      NOT older than the running firmware unless "allow older version" was ticked (cb::version_older).
//   3. Everything is streamed into the PASSIVE app slot (the running firmware is untouched); the bytes before the
//      signature sector are hashed on the way (SHA-256), the sector itself is kept in RAM.
//   4. esp_ota_end() verifies the SHA-256 the build appended; the image must end before the signature sector; then
//      the signature must verify with a key this unit trusts (2 manufacturer keys, optional owner key). Only then
//      the slot becomes the boot slot, and the peers reachable right now are remembered. Reboot.
//   5. The new firmware runs on probation (cb::update_verdict): confirmed after >= 60 s when self-test, WiFi,
//      status page and the previously reachable peers are fine; otherwise back to the old firmware after 5 min.
//      A crash or watchdog reset during probation makes the bootloader roll back by itself.
//
// USB flashing is NOT checked (physical access = threat D, hardening stage C). The owner key and "owner only" live in
// NVS "cb_set" (buyer data: the factory reset removes them).
//
// Console (USB, tests): update-status, update-keys, update-owner <64 hex>|clear, update-owner-only on|off,
// update-fault crash|nowifi|off (makes the NEXT probation fail on purpose, to prove the rollback; only ever acts
// while an update is on probation, then clears itself).
#pragma once

#include <cstddef>
#include <cstdint>

#include "cb_json.h"

void cb_update_begin();   // early in setup(): probation state; executes an armed test fault
void cb_update_poll();    // from loop(): confirm or roll back a firmware on probation
bool cb_update_skip_wifi();  // test fault "nowifi" armed for this probation boot

// Web task, in this order. Each returns false with a German message for the page; then call cb_update_abort().
bool cb_update_start(size_t total, bool allow_older, char *msg, size_t n);
bool cb_update_write(const uint8_t *data, size_t len, char *msg, size_t n);
bool cb_update_finish(char *msg, size_t n);  // success: reboot follows in ~2 s
void cb_update_abort();

// Owner key (R26), maintenance only - the caller checks the mode. hex = 64 hex digits, or "" / "-" to remove it
// (which also ends "owner only"). German message for the page.
bool cb_update_set_owner_key(const char *hex, char *msg, size_t n);
bool cb_update_set_owner_only(bool on, char *msg, size_t n);

void cb_update_json(cb::Json &j);
