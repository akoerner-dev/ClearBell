// cb_provisioning.h - who the unit is (factory) and how the buyer set it up (user configuration).
//
// Two separate stores since port step 10a (docs/Produktreife.md, decision 06.11.2026 "the buyer never flashes"):
//
//   FACTORY  partition `fctry`, written ONCE at the factory over USB (tools/cb.py werk), never changed in operation,
//            survives the factory reset: the DEVICE CODE printed on the label (16 characters). It is the WPA2
//            password of the setup access point and the default password of the status page. The unit number
//            (6 hex characters) comes from the MAC and is not stored.
//
//   USER     NVS namespace "cb_user" in the normal `nvs` partition, written by the setup page (and pairing):
//            WiFi, flat (EG/OG, indoor only), link keys and peer numbers, phones and topics of the remote opening,
//            own status-page password. The factory reset erases exactly this (plus settings, door log, own tones).
//
// Legacy (schema 1, development boards before step 10a): `fctry` held everything. On the first start such data is
// copied once into "cb_user"; the device password becomes the device code. After that `fctry` is only read for
// the code - so a factory reset works on those boards too.
//
// Not configured (no WiFi) -> setup mode with the setup access point; nothing else runs.
#pragma once

#include <cstddef>
#include <cstdint>

#include "cb_protocol.h"

struct CbProvisioning {
  bool valid;             // WiFi configured and the role fits this image -> normal operation
  const char *problem;    // why not valid (log, console, setup page), nullptr if valid
  bool factory_ok;        // device code present (else: unit was never through the factory step)
  cb::UnitId role;        // outdoor (fixed by the image) or EG/OG (chosen on the setup page)
  char serial[7];         // unit number from the MAC, e.g. "f3ac94" - hostname clearbell-<serial>.local
  char name[24];          // label for logs and the page title ("AU-1" on dev boards, else "ClearBell <serial>")
  char dev_code[17];      // label code (factory)
  char wifi_ssid[33];
  char wifi_pass[65];
  char dev_pass[33];      // effective status-page password: own one, else the device code
  // Link keys, indexed by the PEER's UnitId. Outdoor: [EG] and [OG]; indoor: [Outdoor] only.
  uint8_t key[4][cb::kKeyLen];
  bool has_key[4];
  char peer_serial[4][7];  // unit number of each paired peer ("" = unknown: found by role via mDNS)
  uint32_t peer_ip[4];     // optional fixed IPv4 (development), 0 = not set
  // Remote opening (port steps 9/10c). Topics and the app link are DERIVED from the EG link key (cb::remote_names)
  // on the outdoor unit and on the EG indoor unit - nothing to configure. Empty while EG is not paired.
  char topic_out[65];
  char topic_in[65];
  char fern_link[160];     // outdoor: http-shortcuts://<shortcut id>?token={token}
  char shortcut_id[37];
  // Phones by slot (0..3), "" = free. Outdoor: id + key; EG indoor: id only (to list and remove them).
  char phone_id[4][9];
  char phone_key[4][65];
  int phones;
};

// Boot step 6: reads factory and user data (migrates legacy data once). Never fails hard.
void cb_provisioning_begin();
const CbProvisioning &cb_prov();

// ---- Writing (setup page, console). Takes effect after the reboot that follows a save. ----
bool cb_prov_set_wifi(const char *ssid, const char *pass);
bool cb_prov_set_role(cb::UnitId role);  // indoor: EG or OG
// Pairing (cb_pairing): link key and unit number of `peer`; remove = unpair.
bool cb_prov_set_link(cb::UnitId peer, const uint8_t key[32], const char *peer_serial);
bool cb_prov_remove_link(cb::UnitId peer);
// Phones (10c). store: same id = replace, else a free slot; key_hex "" on the indoor unit. -> slot or -1 (full).
int cb_prov_phone_store(const char *id, const char *key_hex);
bool cb_prov_phone_forget(const char *id);
// Own status-page password (stored in cb_user "web_pass", takes effect at once in cb_prov().dev_pass);
// "" = back to the device code from the label. The factory reset erases it too.
bool cb_prov_set_web_pass(const char *pw);
bool cb_prov_has_own_web_pass();
// Factory reset: erases the buyer's data (WiFi, keys, phones, own password, settings, door log). The device code
// stays - the label keeps working. Own tones are restored by the caller (cb_audio). Returns true on success.
bool cb_prov_factory_reset();
