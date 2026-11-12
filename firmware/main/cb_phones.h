// cb_phones.h - phones for the remote opening, managed on each indoor unit's page (port step 10c, R27).
//
// Buyer's view (own indoor unit, maintenance = button in the flat, on the PHONE in the home WiFi):
//   "Handy hinzufügen", give it a short name (e.g. "Anna"). The page then shows ONE link:
//     "ntfy: Nachrichten abonnieren"  ntfy://ntfy.sh/<push topic of this phone> - the ntfy app subscribes; ring
//     pushes arrive there with a "Tür öffnen" button that sends the token to the phone's command topic itself.
//   "Test" sends a push to that phone, "Link" shows the link again, "Entfernen" drops the phone at once (lost phone).
//
// Why on the indoor unit: each household manages its own phones (the outdoor unit belongs to both). The key is
// generated here and travels to the outdoor unit inside an authenticated Event, encrypted with the link key
// (protocol v2 authenticates but does not encrypt); the flat is the sender's link. Both units derive the phone's
// two topics from the key (cb::phone_topics). The indoor unit keeps the key for its outage notice and the link.
//
// Both flats, up to cb::kPhonesPerFlat phones each (R27, FW-B-22).
#pragma once

#include <esp_http_server.h>

#include "cb_json.h"
#include "cb_protocol.h"

void cb_phones_register(httpd_handle_t s);
void cb_phones_poll();  // from loop(): sends pending phone events (indoor)
void cb_phones_json(cb::Json &j);
// Outdoor: PhoneAdd / PhoneRemove from an indoor unit; indoor: results of our own events. From loop.
void cb_phones_on_event(cb::UnitId from, cb::EventCode code, const uint8_t *data, size_t len);
void cb_phones_on_event_result(cb::UnitId to, cb::EventCode code, bool ok);
