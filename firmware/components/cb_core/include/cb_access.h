// cb_access.h - the access matrix of the web endpoints (security catalogue RV-45, VK-08 part 2): which endpoint may
// act in which mode and state. Before, these rules were spread over the handlers (may_change, precondition, mode
// checks) and untested. Hardware-free, PC-tested in test/host/test_access.cpp.
//
// Layering on the device: authorize() (cb_status) first does the cross-site check (cb_http_guard) and the login (or
// lets a client of the setup access point through); THEN the handler asks mode_access() whether the action is allowed
// now. endpoint_info() documents per endpoint whether it writes, needs a login and is cross-site checked - the test
// holds the invariant "every writing endpoint is checked" (only the indoor /pair proves the device code itself).
//
// The table describes the behaviour of 14.11.2026 unchanged. Changes of the rules (security plan Phase 2, decisions
// E2/E3: automatic access point, pairing only with the button) are made HERE, with the test changed first.
#pragma once

#include <cstdint>

#include "cb_modes_logic.h"

namespace cb {

enum class Endpoint : uint8_t {
  Page, StatusJson, ToneUpload, Update, SettingsVolume, SettingsTone, SettingsLed, SettingsOwnerKey, PasswordNonce,
  PasswordChange, SetupInfo, SetupScan, SetupSave, SetupReset, PairList, PairStart, PairRemove, DoorPulse,
  PairAccept, OutdoorMaint, FlatName, PhoneAdd, PhoneRemove, PhoneLink, PhoneTest,
  Count
};

struct EndpointInfo {
  const char *path;
  bool writes;          // POST / PUT
  bool needs_login;     // through authorize(): login or setup access point
  bool origin_checked;  // cross-site check of authorize()
};
EndpointInfo endpoint_info(Endpoint e);

struct AccessInput {
  Mode mode = Mode::Normal;
  bool from_ap = false;        // request came in over the setup access point
  bool provisioned = false;    // WiFi set up and the role fits the image
  bool flat_set = false;       // indoor: key/flat chosen (EG or OG)
  bool indoor_paired = false;  // indoor: has the link key of the outdoor unit
  bool target_paired = false;  // outdoor, PairStart: the flat to be paired is paired already (= replacing)
};

struct Access {
  bool ok;
  const char *status;   // HTTP status line, "200 OK" when allowed
  const char *message;  // German text for the page
};

Access mode_access(Endpoint e, const AccessInput &in);

}  // namespace cb
