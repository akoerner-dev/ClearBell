// cb_remote.h - pushes over ntfy.sh: remote opening (outdoor unit) and outage notice (indoor units). Decision R27,
// Softwarearchitektur 4.5. The security argument is in components/cb_core/include/cb_remote_logic.h.
//
// Outdoor unit - remote opening:
//   ring at EG or OG -> loop draws one token per phone of that flat -> task "ntfy_tx" sends each phone a push to its
//   own push topic, with an ntfy "http" button that POSTs "CBR2 <token>" to the phone's own command topic -> task
//   "ntfy_rx" reads the command topics of all phones in ONE stream and hands each message to loop() -> loop checks
//   it (cb::RemoteAuth: topic = phone, token for exactly this phone, fresh, unused) and asks the door task
//   (cb_door_open, same lock-out as every other path) -> push "Tür per App geöffnet" to the phones of that flat and
//   an entry in the door log (SA 6.4). After every start the unit tells all phones (SA 7.5: power cut = tamper sign).
//
// Indoor units - outage notice (FW-B-17): when the outdoor unit has not answered for kOutageMs, the indoor unit
// pushes "Außeneinheit nicht erreichbar" to the phones of its flat, and "wieder erreichbar nach N min" when it is
// back. Not in the first minutes after a start (the outdoor unit may still be booting).
//
// Both connections are HTTPS with certificate check against the IDF certificate bundle (Mozilla list) - no
// pinning, ntfy's certificates change every few weeks. TLS buffers live in PSRAM (sdkconfig.defaults).
// At most one ring push per flat every kRingPushGapMs; rings of a measurement series ("ring eg 50") never push.
// No internet -> no push and no remote opening; ringing and the indoor door command do not depend on it.
//
// Console: remote (state and counters), remote-push (test push to all phones of this unit).
#pragma once

#include <cstdint>

#include "cb_json.h"
#include "cb_protocol.h"
#include "cb_remote_logic.h"

void cb_remote_begin();                    // after cb_net_begin
void cb_remote_poll();                     // from loop(): outdoor checks received commands; indoor watches the link
void cb_remote_on_ring(cb::UnitId flat);   // from loop(): a visitor rang at `flat` (not for series rings), outdoor
void cb_remote_json(cb::Json &j);          // status page: "remote": {...}
// Phones at runtime (from loop): an indoor unit added / removed a phone of its flat (outdoor).
bool cb_remote_phone_add(const char *id, const char *key_hex, uint8_t flat);
bool cb_remote_phone_remove(const char *id, uint8_t flat);
// VK-01: the pairing of `flat` was removed or replaced - drop all its phones and tokens at once. Any task; the loop
// does the work (the RemoteAuth belongs to it). Outdoor only.
void cb_remote_drop_flat(uint8_t flat);
// Test push to one phone (status page "Test"). Indoor (FW-B-34, 1): the push carries the button "Ja, angekommen"; the
// unit then polls the phone's command topic for up to 5 min - cb_remote_confirm_state() says how it went. A new test
// replaces a waiting one. Web task. False for an invalid key.
bool cb_remote_test(const char *id, const char *key_hex);
enum class CbConfirm : uint8_t { None, Waiting, Ok, Timeout };
CbConfirm cb_remote_confirm_state(char id[cb::kPhoneIdMax + 1], uint32_t *left_s);
void cb_remote_confirm_forget(const char *id);  // phone removed: its result is no longer shown
// FW-B-34 (4): "this subscription is invalid - delete it in ntfy" to one phone (removed) or to every stored phone
// (before the factory reset). why: German, e.g. "Handy entfernt". Any task. The outdoor unit says it to the phones of
// a flat by itself when that flat's pairing goes (cb_remote_drop_flat).
void cb_remote_farewell(const char *key_hex, const char *why);
void cb_remote_farewell_all(const char *why);
// No push waiting or being sent - a planned restart waits for this (at most a few seconds, cb_setup).
bool cb_remote_idle();
// Indoor, FW-B-32: the outdoor unit removed our pairing - no outage notices any more.
void cb_remote_unpaired();
