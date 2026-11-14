// cb_pairing.h - pairing indoor <-> outdoor by device code, maintenance from inside, door pulse setting (port step
// 10b). The cryptography is cb_pairing_logic.h (PC-tested); this file is the web and network glue.
//
// Buyer's view, on the OUTDOOR unit's page (card "Kopplung"):
//   "Inneneinheiten suchen" lists the ClearBell indoor units in the network (mDNS "_clearbell._udp"), each with its
//   unit number and flat. Type the device code from that unit's label, "Koppeln". Both units restart (5 s) and
//   are paired. Every pairing - also the FIRST one of a free flat - and removing one needs the outdoor unit in
//   maintenance opened by ITS button, or in setup (R31, RV-08/RV-33: otherwise a device with the outdoor login, or
//   the other household with a second indoor unit, could pair itself and open the door). Rules: cb_access.h.
//   The indoor unit accepts a pairing while it is not paired, or in maintenance/setup (button in the flat).
//
// On the INDOOR unit's page in maintenance: "Wartung der Außeneinheit öffnen" - the outdoor unit enters maintenance
// for 10 min without opening the outdoor housing. Sent as an authenticated Event (cb_link); the button press at the
// indoor unit is the physical presence. This maintenance "from inside" opens only tone upload, update and pulse
// length (R31) - no pairing, no update key, no password.
//
// Outdoor, maintenance: door pulse length 100..5000 ms (the buyer's opener differs from ours, F-3) and - with the
// button - removing a pairing.
#pragma once

#include <esp_http_server.h>

#include "cb_json.h"
#include "cb_protocol.h"

void cb_pairing_begin();                       // after cb_provisioning_begin
void cb_pairing_register(httpd_handle_t s);    // web handlers (cb_status calls it)
void cb_pairing_poll();                        // from loop(): sends a pending maintenance request (indoor)
void cb_pairing_json(cb::Json &j);             // status page: "pairing": {...}
// Event from a peer (cb_net -> main.cpp): outdoor handles MaintenanceRequest here. Call from loop.
void cb_pairing_on_event(cb::UnitId from, cb::EventCode code, const uint8_t *data, size_t len);
// Indoor: result of our own FlatName event (FW-B-21) - failed ones are sent again after 30 s. Call from loop.
void cb_pairing_on_event_result(cb::UnitId to, cb::EventCode code, bool ok);
