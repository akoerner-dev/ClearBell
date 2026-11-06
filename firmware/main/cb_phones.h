// cb_phones.h - phones for the remote opening, managed on the EG indoor unit's page (port step 10c).
//
// Buyer's view (EG indoor unit, maintenance = button in the flat, on the PHONE in the home WiFi):
//   "Handy hinzufügen", give it a short name (e.g. "Anna"). The page then shows two buttons:
//     1. "ntfy abonnieren"         ntfy://ntfy.sh/<push topic>  - the ntfy app subscribes, bell pushes arrive there
//     2. "Kurzbefehl importieren"  https://http-shortcuts.rmy.ch/import?url=<this unit>/phone/<one-time token>.json
//        HTTP Shortcuts imports a ready shortcut: it signs the token of the push with this phone's key and sends
//        the answer to the command topic. Its id is fixed, so the push button opens it - nothing to type.
//   "Entfernen" next to each phone locks it out at once (lost phone).
//
// Why on the indoor unit: each household manages its own phones (the outdoor unit belongs to both). The key is
// generated here and travels to the outdoor unit inside an authenticated Event, encrypted with the link key
// (protocol v2 authenticates but does not encrypt). The one-time download works 10 min, three times at most, and
// only in the home network; afterwards the key exists only on the phone and in the outdoor unit.
//
// Remote opening is EG only (SA 6.4); an OG unit shows that instead of the buttons.
#pragma once

#include <esp_http_server.h>

#include "cb_json.h"
#include "cb_protocol.h"

void cb_phones_register(httpd_handle_t s);
void cb_phones_poll();  // from loop(): sends pending phone events (indoor)
void cb_phones_json(cb::Json &j);
// Outdoor: PhoneAdd / PhoneRemove from the EG unit; indoor: results of our own events. From loop.
void cb_phones_on_event(cb::UnitId from, cb::EventCode code, const uint8_t *data, size_t len);
void cb_phones_on_event_result(cb::UnitId to, cb::EventCode code, bool ok);
