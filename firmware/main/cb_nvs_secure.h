// cb_nvs_secure.h - Härtung Stufe B, part B1: the NVS partition "nvs" encrypted with a key derived from an HMAC key
// in eFuse BLOCK_KEY0 (docs/Haertung_Stufe_B.md).
//
// Why a wrapper around nvs_flash_init(): the Arduino core opens "nvs" in PLAIN mode before setup() on every start, and
// a plain open over encrypted data ERASES the entries (header CRC mismatch, nvs_page.cpp). So the decision has to be
// made exactly there: main/CMakeLists.txt links with --wrap=nvs_flash_init, and __wrap_nvs_flash_init() below
//   - no key in eFuse              -> plain, as before (one image for burned and unburned boards)
//   - key, marker with its print   -> encrypted
//   - key, no marker               -> MIGRATION: read every entry in plain, erase, open encrypted, write back, marker
//   - key, marker of another key   -> data unreadable: erase, open encrypted empty (= not set up)
//   - marker but no key            -> refuse to open (a plain open would erase); unit stays unconfigured
// The marker (magic + 8-byte fingerprint of the derived keys) lives in the otherwise unused partition "nvs_keys".
//
// CONFIG_NVS_ENCRYPTION stays OFF on purpose: with it, nvs_flash_init() BURNS a random key by itself on the first
// start. Here the key is burned only by the console command "haertung nvs-schluessel BRENNEN <unit number>" (USB =
// physical presence); the key is generated on the chip and never leaves it (HB-E1).
//
// Test build (cmake -D CB_HB_TESTKEY=1, tools/cb.py bauen innen-hbtest): a fixed key from software instead of the
// eFuse, so encryption and migration can be tried without burning anything; "haertung klartext JA" goes back.
#pragma once

#include <cstddef>

#include "cb_json.h"

void cb_nvs_secure_begin();         // boot step 6 (before provisioning): logs what the wrapper did, console commands
bool cb_nvs_secure_encrypted();     // "nvs" is open encrypted
const char *cb_nvs_secure_state();  // short English state for log, status page and tools
void cb_nvs_secure_json(cb::Json &j);
