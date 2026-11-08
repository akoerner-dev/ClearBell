// cb_touch_logic.h - touch evaluation, hardware-free (Softwarearchitektur 5.6, M-27, M-34). PC tests:
// test/host/test_touch.cpp. The device glue (main/cb_touch.cpp) only feeds raw values and the time.
//
// THREE LAYERS
//
// 1 TouchChannel - one electrode. Evaluates RELATIVE to a baseline (fixed thresholds do not survive a change of
//   season). Baseline = the electrode's value at rest:
//     * learned after start only once the value is calm (nobody touching, SA 5.6 e),
//     * slowly tracked (time constant ~ 1 min) while the value is near the baseline - follows temperature and
//       humidity drift,
//     * FROZEN while the value is clearly raised (finger, hand near the wire): it must never creep up onto a
//       resting finger (SA 5.6 #2),
//     * relearned when something lies on the key for longer than kStuckMs (cloth, towel - M-34 a).
//   "Pressed" with hysteresis (on above +kOnPercent, off below +kOffPercent) and a minimum hold time.
//
//     * taken over when it sits STEADY above the tracking band but clearly below a touch for kOffsetAdoptMs
//       (FW-B-06_Baseline_eingefroren).
//
// 2 BellKeys (outdoor, two channels EG/OG) - a ring per press, decided after the minimum hold plus a short guard
//   time. Plausibility (SA 5.6 #3): if BOTH keys are raised at the same time - or the other one rises during the
//   guard time (FW-B-07_Beide_Tasten_klingelt) - it is not a finger but wetting -> no ring.
//
// 3 DoorKey (indoor, one channel) - a wrong trigger here opens the front door, so (SA 5.6 a-f, R18):
//     * triggers only on RELEASE (a permanently wet sensor never triggers),
//     * within 2 min after a ring AT THIS FLAT ("armed") a short tap opens,
//     * otherwise hold 3 s -> a tick says "release now" -> release within 4 s -> opens,
//     * held longer -> rejected (and the channel relearns its baseline),
//     * a tap must be longer than the indoor minimum hold time (longer than outdoors).
//   The lock-out after a door command is enforced by the outdoor unit for all paths.
//
// Threshold values are DRAFTS from the bring-up on the desk (dry electrodes, PC-USB): touch +280 ... +650 %,
// wiping with a cloth up to +37 %, hand at the wire up to +20 %, water on the free electrode ~ 0 %.
// Final values at the mounted unit (Projektplan P5, Feuchtetest).
#pragma once

#include <cstdint>

namespace cb {

// ---- Draft parameters ------------------------------------------------------------------------------------
constexpr float kOnPercent = 100.0f;      // pressed above +100 % of the baseline
constexpr float kOffPercent = 50.0f;      // released below +50 %
constexpr float kTrackBand = 5.0f;        // baseline follows slowly while delta is between -2 % and +5 %
constexpr float kFastDownPercent = 2.0f;  // more than 2 % BELOW the baseline: follow down within ~1 s
constexpr uint32_t kTrackTauMs = 60000;   // tracking time constant
constexpr uint32_t kSettleMs = 1000;      // calm phase needed to learn the baseline after start
constexpr float kSettleSpread = 2.0f;     // "calm" = max-min within 2 % during the settle phase
constexpr uint32_t kStuckMs = 10000;      // raised longer than this: something lies on it -> relearn
// FW-B-06_Baseline_eingefroren: a STEADY offset above the tracking band but below the release threshold
// (+kTrackBand ... +kOffPercent, spread within kSettleSpread) is no finger - take it over as the new baseline after
// kOffsetAdoptMs. Otherwise a step (warm-up after start, temperature, moved cable) freezes the baseline for good.
constexpr uint32_t kOffsetAdoptMs = 30000;
constexpr uint32_t kBellMinHoldMs = 60;   // outdoor: minimum hold before a ring
// FW-B-07_Beide_Tasten_klingelt: a hand on both keys raises them some ms apart (08.11.2026: 21 ms). The ring waits
// this long after the minimum hold; if the OTHER key rises above +kOffPercent meanwhile, it is "both" -> no ring.
constexpr uint32_t kBellBothGuardMs = 120;
constexpr uint32_t kDoorMinHoldMs = 150;  // indoor: minimum hold (longer than outdoors, SA 5.6 c)
constexpr uint32_t kTapMaxMs = 2000;      // indoor: a tap is shorter than this (SA 5.6 a)
constexpr uint32_t kHoldMs = 3000;        // indoor outside the window: hold this long ...
constexpr uint32_t kReleaseWindowMs = 4000;  // ... then release within this time after the tick
constexpr uint32_t kArmedWindowMs = 2UL * 60 * 1000;  // R18: 2 min after a ring at this flat

// ---- Layer 1 ---------------------------------------------------------------------------------------------
class TouchChannel {
 public:
  enum class Event : uint8_t { None, Pressed, Released, Stuck };

  // Feed one raw value (any unit; on the S3 it RISES on touch). Returns what happened with this sample.
  Event update(uint32_t raw, uint32_t now_ms);

  bool ready() const { return ready_; }      // baseline learned
  bool pressed() const { return pressed_; }  // debounced state
  bool raised() const { return raised_; }    // above the on-threshold right now (not debounced)
  uint32_t pressed_since() const { return pressed_at_; }
  float baseline() const { return baseline_; }
  float delta_percent() const { return delta_; }
  uint32_t raw() const { return raw_; }
  uint32_t relearn_count() const { return relearns_; }
  uint32_t offset_adopt_count() const { return adopts_; }
  void relearn(uint32_t now_ms);  // forget the baseline, learn anew once calm

 private:
  bool ready_ = false, pressed_ = false, raised_ = false;
  float baseline_ = 0, delta_ = 0;
  uint32_t raw_ = 0, last_ms_ = 0, raised_at_ = 0, pressed_at_ = 0, relearns_ = 0, adopts_ = 0;
  // steady offset (FW-B-06)
  uint32_t offset_start_ = 0, offset_min_ = 0, offset_max_ = 0, offset_n_ = 0;
  double offset_sum_ = 0;
  // settle phase
  bool settling_ = true;
  uint32_t settle_start_ = 0, settle_min_ = 0, settle_max_ = 0;
  double settle_sum_ = 0;
  uint32_t settle_n_ = 0;
};

// ---- Layer 2: outdoor -------------------------------------------------------------------------------------
class BellKeys {
 public:
  enum class Out : uint8_t { None, RingEG, RingOG, Rejected };
  // Feed both raw values of one sampling round. At most one result per call.
  Out update(uint32_t raw_eg, uint32_t raw_og, uint32_t now_ms);
  const TouchChannel &eg() const { return eg_; }
  const TouchChannel &og() const { return og_; }
  uint32_t rejected_count() const { return rejected_; }

 private:
  TouchChannel eg_, og_;
  bool fired_eg_ = false, fired_og_ = false;  // a press rings once
  uint32_t rejected_ = 0;
};

// ---- Layer 3: indoor --------------------------------------------------------------------------------------
class DoorKey {
 public:
  enum class Out : uint8_t {
    None,
    Open,          // send the door command
    ReleaseNow,    // the 3 s are reached: play the tick
    Rejected,      // held too long / too short outside the window: nothing happens
  };
  Out update(uint32_t raw, uint32_t now_ms);
  void arm(uint32_t now_ms) { armed_at_ = now_ms; armed_ = true; }  // a ring at this flat arrived
  void disarm() { armed_ = false; }  // after a successful opening the window ends (draft decision)
  bool armed(uint32_t now_ms) const { return armed_ && now_ms - armed_at_ < kArmedWindowMs; }
  const TouchChannel &channel() const { return ch_; }
  uint32_t rejected_count() const { return rejected_; }

 private:
  TouchChannel ch_;
  bool armed_ = false, tick_given_ = false;
  uint32_t armed_at_ = 0, rejected_ = 0;
};

}  // namespace cb
