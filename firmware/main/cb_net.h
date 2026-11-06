// cb_net.h - WiFi, UDP and the protocol engine (cb::Link) on the device. Port step 3.
//
// WiFi (Softwarearchitektur 5.8): starts in the background, the unit is usable without it. Lost
// connection -> reconnect driven by events with growing back-off (1, 2, 4 ... 60 s); a running attempt is
// never aborted (the V0.1 mistake). Country code DE + scan over all channels, otherwise a hidden network
// on channel 12/13 is never found (bring-up finding 06.11.2026).
//
// Modem sleep (M-23, decision 06.11.2026): on while idle (saves 10-20 mA). The outdoor unit switches it
// OFF while a ring is in flight and back on when all rings are settled - measured: with only the receiver
// asleep every ack arrived within the 300 ms window.
//
// UDP port 4210. Received datagrams are copied into a FreeRTOS queue by the UDP task and processed in
// loop(): the Link object is owned by the loop alone (Softwarearchitektur 3.2, rules 2 and 3).
//
// Peer addresses, in this order: fixed IP from provisioning, else the address an authenticated frame
// last came from, else mDNS (resolved in the background): clearbell-<unit number>.local if pairing stored the number,
// otherwise the ClearBell announcing the wanted role in its "_clearbell._udp" record (port step 10b).
//
// Console: net (status and counters), ring eg|og [count] (outdoor only: ring now, or a measurement series
// with ack-time statistics), sleep 0|1|auto (modem sleep), wifi-drop (reconnect test).
#pragma once

#include "cb_json.h"
#include "cb_protocol.h"

void cb_net_begin();  // only call with valid provisioning
void cb_net_poll();   // from loop()

bool cb_net_wifi_up();
bool cb_net_peer_reachable(cb::UnitId peer);
// Outdoor: start ringing at a flat. false = failed at once (no WiFi, no free slot) -> error tone at once.
bool cb_net_ring(cb::UnitId flat);

// Indoor: send a door command (REQUEST -> CHALLENGE -> CONFIRM -> ACK). false = failed at once.
bool cb_net_door_request();
// Sends an authenticated, acknowledged event (port step 10b). false = failed at once.
bool cb_net_send_event(cb::UnitId peer, cb::EventCode code, const uint8_t *data, size_t len);

// Status page: "radio" and "protocol" objects (call from loop: the engine belongs to loop).
void cb_net_json(cb::Json &j);

// Application hooks, implemented in main.cpp (weak defaults log or refuse).
void cb_on_ring_received();                        // indoor: a visitor rang (ack already sent)
void cb_on_ring_result(cb::UnitId flat, bool ok);  // outdoor: acknowledged or finally failed
// Outdoor: a flat proved its key with a fresh challenge - Opened, Locked or NotAllowed? Must not block.
cb::DoorResult cb_on_door_decide(cb::UnitId flat);
// Outdoor: the DoorAck(Opened) is out - now open. The ONLY caller of the door path from the network.
void cb_on_door_open(cb::UnitId flat);
// Indoor: answer of the outdoor unit (answered = false: no answer within the retries).
void cb_on_door_result(cb::DoorResult r, bool answered, uint32_t elapsed_ms);
// An event from a peer arrived (ack already sent). Called from loop.
void cb_on_event(cb::UnitId from, cb::EventCode code, const uint8_t *data, size_t len);
void cb_on_event_result(cb::UnitId to, cb::EventCode code, bool ok);
