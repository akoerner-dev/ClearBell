// cb_touch.h - touch keys on the device. Port step 6. The evaluation itself (baseline, thresholds, window,
// plausibility) is cb_touch_logic.h in the core; this file samples the electrodes and acts on the result.
//
//   outdoor: TOUCH_EG (IO4) / TOUCH_OG (IO5) -> ring at that flat (cb_net_ring)
//   indoor:  TOUCH (IO4)                     -> door command (cb_net_door_request), with the armed window
//            after a ring at this flat; the "release now" tick of the 3-s hold comes from cb_audio
//
// Sampling every 20 ms from loop() through the Arduino touch driver (on the S3 the IDF driver touch_sens with
// its hardware filter; the raw value RISES on touch). Boot step 11: the baseline is learned during the first
// calm second - do not touch the keys right after power-on.
//
// Console:
//   touch                      raw value, baseline, delta, state per channel, counters
//   touch-log <s>              every 100 ms one line per channel for <s> seconds (for the hand tests)
//   touch-relearn              learn the baseline anew (keys must be untouched)
//   touch-sim [eg|og|both] <%> <ms> TEST: feed "baseline + <%>" into the evaluation for <ms> - exercises the whole
//                              chain (ring / door command) without a finger. USB only, like `door`.
#pragma once

#include "cb_json.h"

void cb_touch_begin();
void cb_touch_poll();
void cb_touch_arm();     // indoor: a ring at this flat arrived -> short tap opens for 2 min
void cb_touch_disarm();  // indoor: door opened -> window ends
void cb_touch_json(cb::Json &j);  // status page: raw value + baseline per key (SA 5.6 measure 5)
