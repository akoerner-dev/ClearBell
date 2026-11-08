// test_touch.cpp - touch evaluation with simulated raw values in the ranges measured in the bring-up
// (IN-1: rest ~29 000, finger +280..+650 %; wiping with a cloth up to +37 %; hand at the wire ~+20 %).
// Covers Softwarearchitektur 11.2: "nasser Lappen auf der Elektrode, beide Taster außen zugleich -> keine
// Auslösung" and "Tipp innen im Fenster; außerhalb des Fensters; 3 s halten -> öffnet; öffnet nicht; öffnet".
#include "cb_touch_logic.h"
#include "mini_test.h"

using namespace cb;

namespace {

constexpr uint32_t kRest = 29000;
constexpr uint32_t kFinger = 150000;  // ~ +420 %
constexpr uint32_t kStep = 20;        // sampling period of the firmware (ms)

uint32_t pct(float p) { return (uint32_t)(kRest * (1 + p / 100)); }

// Drives a DoorKey for `ms` at `raw`; returns the first non-None output.
DoorKey::Out door(DoorKey &k, uint32_t &t, uint32_t ms, uint32_t raw) {
  DoorKey::Out first = DoorKey::Out::None;
  for (uint32_t x = 0; x < ms; x += kStep, t += kStep) {
    DoorKey::Out o = k.update(raw, t);
    if (o != DoorKey::Out::None && first == DoorKey::Out::None) first = o;
  }
  return first;
}

BellKeys::Out bell(BellKeys &k, uint32_t &t, uint32_t ms, uint32_t eg, uint32_t og, int *rings_eg = nullptr,
                   int *rings_og = nullptr) {
  BellKeys::Out first = BellKeys::Out::None;
  for (uint32_t x = 0; x < ms; x += kStep, t += kStep) {
    BellKeys::Out o = k.update(eg, og, t);
    if (o == BellKeys::Out::RingEG && rings_eg) (*rings_eg)++;
    if (o == BellKeys::Out::RingOG && rings_og) (*rings_og)++;
    if (o != BellKeys::Out::None && first == BellKeys::Out::None) first = o;
  }
  return first;
}

DoorKey ready_door(uint32_t &t) {
  DoorKey k;
  door(k, t, 1500, kRest);  // calm start: baseline learned
  return k;
}

}  // namespace

TEST(touch_baseline_learned_only_when_calm) {
  TouchChannel c;
  uint32_t t = 0;
  // Someone fiddles with the board at start: values jump around -> no baseline yet.
  for (int i = 0; i < 100; i++, t += kStep) c.update(i % 2 ? kRest : pct(20), t);
  CHECK(!c.ready());
  for (int i = 0; i < 60; i++, t += kStep) c.update(kRest, t);  // 1.2 s calm
  CHECK(c.ready());
  CHECK(c.baseline() > kRest * 0.999f && c.baseline() < kRest * 1.001f);
}

TEST(touch_baseline_tracks_drift_but_not_a_resting_finger) {
  TouchChannel c;
  uint32_t t = 0;
  for (int i = 0; i < 60; i++, t += kStep) c.update(kRest, t);
  // Slow drift of +3 % (season, temperature) is followed within a few minutes ...
  for (int i = 0; i < 15000; i++, t += kStep) c.update(pct(3), t);  // 5 min
  CHECK(c.baseline() > kRest * 1.025f);
  // ... a finger is not: 8 s on the key, the baseline does not move.
  float before = c.baseline();
  for (int i = 0; i < 400; i++, t += kStep) c.update(kFinger, t);
  CHECK(c.baseline() == before);
  CHECK(c.pressed());
}

TEST(touch_hand_at_wire_and_wiping_are_no_press) {
  TouchChannel c;
  uint32_t t = 0;
  for (int i = 0; i < 60; i++, t += kStep) c.update(kRest, t);
  int presses = 0;
  for (int i = 0; i < 1250; i++, t += kStep)  // 25 s at +20 % (bring-up AU-1: hand at the OG wire)
    presses += c.update(pct(20), t) == TouchChannel::Event::Pressed;
  for (int i = 0; i < 350; i++, t += kStep)   // 7 s wiping, swinging between +3 and +37 %
    presses += c.update(pct(i % 2 ? 3 : 37), t) == TouchChannel::Event::Pressed;
  CHECK_EQ(presses, 0);
}

TEST(touch_stuck_cloth_relearns_and_recovers) {
  TouchChannel c;
  uint32_t t = 0;
  for (int i = 0; i < 60; i++, t += kStep) c.update(kRest, t);
  int stuck = 0;
  for (int i = 0; i < 700; i++, t += kStep)  // wet cloth lies on it for 14 s
    stuck += c.update(pct(300), t) == TouchChannel::Event::Stuck;
  CHECK_EQ(stuck, 1);
  CHECK(c.ready());  // relearned with the cloth as the new rest value
  for (int i = 0; i < 300; i++, t += kStep) c.update(kRest, t);  // cloth removed: follows down
  CHECK(c.baseline() < kRest * 1.05f);
  for (int i = 0; i < 20; i++, t += kStep) c.update(kFinger, t);  // and a finger works again
  CHECK(c.pressed());
}

TEST(bell_ring_once_per_press_and_min_hold) {
  BellKeys k;
  uint32_t t = 0;
  int eg = 0, og = 0;
  bell(k, t, 1500, kRest, kRest);
  bell(k, t, 40, kFinger, kRest, &eg, &og);  // 40 ms spike: below the minimum hold
  bell(k, t, 200, kRest, kRest, &eg, &og);
  CHECK_EQ(eg, 0);
  bell(k, t, 2000, kFinger, kRest, &eg, &og);  // a real press, held 2 s: exactly one ring
  bell(k, t, 200, kRest, kRest, &eg, &og);
  bell(k, t, 300, kRest, kFinger, &eg, &og);  // then OG
  CHECK_EQ(eg, 1);
  CHECK_EQ(og, 1);
}

TEST(bell_both_keys_at_once_is_wetting_no_ring) {
  BellKeys k;
  uint32_t t = 0;
  int eg = 0, og = 0;
  bell(k, t, 1500, kRest, kRest);
  CHECK(bell(k, t, 1000, kFinger, kFinger, &eg, &og) == BellKeys::Out::Rejected);
  CHECK_EQ(eg + og, 0);
  CHECK_EQ(k.rejected_count(), 1);
}

// FW-B-07_Beide_Tasten_klingelt (08.11.2026, AU-1): a flat hand on both keys raised EG 21 ms before OG; the ring
// went out before the plausibility rule saw both. Now the ring waits for a guard time.
TEST(bell_hand_on_both_keys_staggered_no_ring) {
  const uint32_t lags[] = {20, 60, 100, 140};  // OG rises this many ms after EG
  for (uint32_t lag : lags) {
    BellKeys k;
    uint32_t t = 0;
    int eg = 0, og = 0;
    bell(k, t, 1500, kRest, kRest);
    bell(k, t, lag, kFinger, kRest, &eg, &og);
    bell(k, t, 3000, kFinger, kFinger, &eg, &og);
    bell(k, t, 500, kRest, kRest, &eg, &og);
    CHECK_EQ(eg + og, 0);
    CHECK_EQ(k.rejected_count(), 1);
  }
}

TEST(bell_single_finger_rings_despite_neighbour_crosstalk) {
  BellKeys k;
  uint32_t t = 0;
  int eg = 0, og = 0;
  bell(k, t, 1500, kRest, kRest);
  bell(k, t, 1000, kFinger, pct(30), &eg, &og);  // finger on EG, OG sees +30 % (below the release threshold)
  bell(k, t, 300, kRest, kRest, &eg, &og);
  CHECK_EQ(eg, 1);
  CHECK_EQ(og, 0);
}

TEST(bell_ring_latency_includes_guard) {
  BellKeys k;
  uint32_t t = 0;
  bell(k, t, 1500, kRest, kRest);
  uint32_t start = t, rang_at = 0;
  for (uint32_t x = 0; x < 1000 && !rang_at; x += kStep, t += kStep)
    if (k.update(kFinger, kRest, t) == BellKeys::Out::RingEG) rang_at = t;
  CHECK(rang_at != 0);
  // raised + debounce (60) + ring min hold (60) + guard (120), sampled every 20 ms: well within SA 1.3 (0.5 s total)
  CHECK(rang_at - start <= 2 * kBellMinHoldMs + kBellBothGuardMs + 2 * kStep);
}

// FW-B-06_Baseline_eingefroren (08.11.2026, AU-1): rest sat at +11 ... +14 % for hours - above the tracking band,
// so the baseline was frozen for good.
TEST(touch_steady_offset_is_taken_over) {
  TouchChannel c;
  uint32_t t = 0;
  for (int i = 0; i < 60; i++, t += kStep) c.update(kRest, t);
  for (uint32_t x = 0; x < kOffsetAdoptMs + 2000; x += kStep, t += kStep)  // +13.6 %, +-0.3 % noise
    c.update(pct(13.6f + ((x / kStep) % 3 - 1) * 0.3f), t);
  CHECK_EQ(c.offset_adopt_count(), 1);
  CHECK(c.delta_percent() > -2 && c.delta_percent() < 2);
  for (int i = 0; i < 20; i++, t += kStep) c.update(kFinger, t);  // a finger still works
  CHECK(c.pressed());
}

TEST(touch_unsteady_or_short_offset_is_not_taken_over) {
  TouchChannel c;
  uint32_t t = 0;
  for (int i = 0; i < 60; i++, t += kStep) c.update(kRest, t);
  for (int i = 0; i < 3000; i++, t += kStep) c.update(pct(i % 2 ? 8 : 40), t);  // 60 s wiping/hand moving
  for (uint32_t x = 0; x < kOffsetAdoptMs - 5000; x += kStep, t += kStep) c.update(pct(20), t);  // 25 s hand
  CHECK_EQ(c.offset_adopt_count(), 0);
  CHECK(c.baseline() < kRest * 1.01f);
  // a resting finger (far above the release threshold) is never taken over either
  for (int i = 0; i < 400; i++, t += kStep) c.update(kFinger, t);
  CHECK_EQ(c.offset_adopt_count(), 0);
}

TEST(door_tap_in_window_opens_outside_not) {
  uint32_t t = 0;
  DoorKey k = ready_door(t);
  CHECK(door(k, t, 400, kFinger) == DoorKey::Out::None);  // nothing while held (release triggers)
  CHECK(door(k, t, 100, kRest) == DoorKey::Out::Rejected);  // not armed: a tap does nothing
  k.arm(t);
  door(k, t, 400, kFinger);
  CHECK(door(k, t, 100, kRest) == DoorKey::Out::Open);
}

TEST(door_window_expires_after_2_minutes) {
  uint32_t t = 0;
  DoorKey k = ready_door(t);
  k.arm(t);
  door(k, t, kArmedWindowMs + 1000, kRest);
  door(k, t, 400, kFinger);
  CHECK(door(k, t, 100, kRest) == DoorKey::Out::Rejected);
}

TEST(door_hold_3s_tick_then_release_opens) {
  uint32_t t = 0;
  DoorKey k = ready_door(t);
  CHECK(door(k, t, kHoldMs + 200, kFinger) == DoorKey::Out::ReleaseNow);
  CHECK(door(k, t, 100, kRest) == DoorKey::Out::Open);
}

TEST(door_held_too_long_or_medium_is_rejected) {
  uint32_t t = 0;
  DoorKey k = ready_door(t);
  door(k, t, kHoldMs + kReleaseWindowMs + 500, kFinger);  // past the release window
  CHECK(door(k, t, 100, kRest) == DoorKey::Out::Rejected);
  k.arm(t);
  door(k, t, 2500, kFinger);  // 2.5 s: too long for a tap, too short for the hold
  CHECK(door(k, t, 100, kRest) == DoorKey::Out::Rejected);
}

TEST(door_too_short_tap_is_rejected_even_armed) {
  uint32_t t = 0;
  DoorKey k = ready_door(t);
  k.arm(t);
  door(k, t, 100, kFinger);  // 100 ms < indoor minimum 150 ms
  CHECK(door(k, t, 100, kRest) == DoorKey::Out::Rejected);
}

TEST(door_wet_cloth_never_opens) {
  uint32_t t = 0;
  DoorKey k = ready_door(t);
  k.arm(t);  // even inside the window
  int opens = 0;
  for (uint32_t x = 0; x < 20000; x += kStep, t += kStep) opens += k.update(pct(300), t) == DoorKey::Out::Open;
  for (uint32_t x = 0; x < 3000; x += kStep, t += kStep) opens += k.update(kRest, t) == DoorKey::Out::Open;
  CHECK_EQ(opens, 0);
  CHECK(k.rejected_count() >= 1);
}
