// cb_remote.h - remote opening by phone over ntfy.sh (outdoor unit only). Port step 9, Softwarearchitektur 4.5.
//
// What happens (the security argument is in components/cb_core/include/cb_remote_logic.h):
//   ring at EG -> loop draws a one-time token -> task "ntfy_tx" publishes a push to topic_out whose button
//   opens the phone app with the token -> the phone publishes "CBR1 <token> <phone> <signature>" to topic_in
//   -> task "ntfy_rx" reads the JSON stream of topic_in and hands each message to loop() -> loop checks it
//   (cb::RemoteAuth) and asks the door task (cb_door_open, same lock-out as every other path) -> push
//   "Tür per App geöffnet" and an entry in the door log (SA 6.4: remote openings are listed one by one).
//
// Both connections are HTTPS with certificate check against the IDF certificate bundle (Mozilla list) - no
// pinning, ntfy's certificates change every few weeks. TLS buffers live in PSRAM (sdkconfig.defaults).
// Pushes only for EG (SA 6.4: remote opening exists only for EG), at most one per kRingPushGapMs; rings of a
// measurement series ("ring eg 50") never push. After every start the unit announces itself with the reset
// reason (SA 7.5: tamper indication by power cut).
//
// Without topics in the factory data (cb.py anlage-fern) the module stays off; everything else is unaffected.
// No internet -> no push and no remote opening; ringing and the indoor door command do not depend on it.
//
// Console: remote (state and counters), remote-push (test push to the phones).
#pragma once

#include "cb_json.h"
#include "cb_protocol.h"

void cb_remote_begin();                    // after cb_net_begin
void cb_remote_poll();                     // from loop(): checks received commands, acts
void cb_remote_on_ring(cb::UnitId flat);   // from loop(): a visitor rang at `flat` (not for series rings)
void cb_remote_json(cb::Json &j);          // status page: "remote": {...}, outdoor only
// Phones at runtime (10c, from loop): the EG indoor unit added / removed a phone.
bool cb_remote_phone_add(const char *id, const char *key_hex);
bool cb_remote_phone_remove(const char *id);
