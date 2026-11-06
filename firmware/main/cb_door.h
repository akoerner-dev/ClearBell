// cb_door.h - the door opener (outdoor unit only). Port step 7, Softwarearchitektur 5.4.
//
// THE ONLY MODULE THAT SWITCHES GPIO47 (CB_PIN_DOOR_DRV). Besides this file, the pin name appears only in
// main.cpp, where the start sequence forces it LOW before anything else. "Who can open the door?" is
// therefore answered by a text search for CB_PIN_DOOR_DRV.
//
// Paths into this module: the door command of an indoor unit (cb_net -> main.cpp), later the remote opening
// (step 9), and the dry-run console command. All of them pass the same lock-out (DoorGuard).
//
// How a pulse runs:
//   cb_door_open() checks the lock-out and queues the request -> task "door" sets GPIO47 HIGH and arms a
//   hardware timer (gptimer) -> the timer INTERRUPT sets GPIO47 LOW. The pulse therefore ends on time even if
//   loop() or the door task itself hangs. The on-time is clamped to kDoorPulseMaxMs. The pin is run as
//   input+output, so the firmware reads back the real level: HIGH during the pulse, LOW afterwards; a pin
//   still HIGH after the pulse is forced LOW and logged as an error.
//
// Console: door-test [ms]  - dry run over USB only (physical access), pulse 100..5000 ms, default 2000 ms.
//          door-status     - openings, last pulse (measured length), lock-out.
#pragma once

#include <cstdint>

#include "cb_json.h"
#include "cb_protocol.h"

void cb_door_begin();
// Lock-out over? (For the DoorAck decision - does not record anything.)
bool cb_door_allowed();
// Starts an opening if the lock-out allows it. `who` is only for the log. Returns false = locked out.
bool cb_door_open(const char *who, uint32_t pulse_ms);
// Pulse length of THIS installation (status page, maintenance; NVS cb_set "pulse"), default kDoorPulseMs.
uint32_t cb_door_pulse_ms();
bool cb_door_set_pulse_ms(int ms);  // 100..5000, false otherwise
// Status page: opener state WITHOUT who opened last (SA 6.4: the outdoor unit shows no flat). Indoor: nothing.
void cb_door_json(cb::Json &j);
