// test_door.cpp - lock-out and pulse limits (cb_door_logic.h).
#include "cb_door_logic.h"
#include "mini_test.h"

using namespace cb;

TEST(door_guard_lockout_for_all_paths) {
  DoorGuard g;
  CHECK(g.allowed(0));  // first opening after boot is always allowed
  g.opened(1000);
  CHECK(!g.allowed(1001));
  CHECK(!g.allowed(1000 + kDoorLockoutMs - 1));
  CHECK(g.allowed(1000 + kDoorLockoutMs));
  CHECK_EQ(g.count(), 1);
}

TEST(door_guard_lockout_across_millis_wrap) {
  DoorGuard g;
  g.opened(0xFFFFFFFFu - 1000);
  CHECK(!g.allowed(2000));                 // 3 s after, across the wrap
  CHECK(g.allowed(kDoorLockoutMs - 1000));  // 5 s after
}

TEST(door_pulse_is_clamped) {
  CHECK_EQ(DoorGuard::clamp_pulse(0), kDoorPulseMinMs);
  CHECK_EQ(DoorGuard::clamp_pulse(2000), 2000);
  CHECK_EQ(DoorGuard::clamp_pulse(600000), kDoorPulseMaxMs);
}
