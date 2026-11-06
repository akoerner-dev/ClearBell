// test_modes.cpp - boot-button gestures and operating modes (cb_modes_logic.h).
#include "cb_modes_logic.h"
#include "mini_test.h"

using namespace cb;

namespace {

// Holds the button at `level` from `t` for `ms` milliseconds in 1-ms steps; returns the first event.
ButtonEvent drive(ButtonGesture &b, bool level, uint32_t &t, uint32_t ms) {
  ButtonEvent first = ButtonEvent::None;
  for (uint32_t i = 0; i < ms; i++, t++) {
    ButtonEvent e = b.update(level, t);
    if (e != ButtonEvent::None && first == ButtonEvent::None) first = e;
  }
  return first;
}

}  // namespace

TEST(button_short_press) {
  ButtonGesture b;
  uint32_t t = 1000;
  CHECK(drive(b, false, t, 100) == ButtonEvent::None);
  CHECK(drive(b, true, t, 200) == ButtonEvent::None);   // nothing while held
  CHECK(drive(b, false, t, 100) == ButtonEvent::Short);  // fires on release
}

TEST(button_bounce_is_ignored) {
  ButtonGesture b;
  uint32_t t = 0;
  // 10 ms spikes below the 30 ms debounce time never count as presses.
  for (int i = 0; i < 20; i++) {
    CHECK(drive(b, true, t, 10) == ButtonEvent::None);
    CHECK(drive(b, false, t, 10) == ButtonEvent::None);
  }
}

TEST(button_long_press_fires_while_held_and_only_once) {
  ButtonGesture b;
  uint32_t t = 0;
  drive(b, false, t, 50);
  CHECK(drive(b, true, t, kLongHoldMs + kDebounceMs - 5) == ButtonEvent::None);
  CHECK(drive(b, true, t, 10) == ButtonEvent::Long);
  CHECK(b.held_long());
  CHECK(drive(b, true, t, 5000) == ButtonEvent::None);  // keeping it pressed: no second Long
  CHECK(drive(b, false, t, 100) == ButtonEvent::None);  // release after Long: no Short
  CHECK(!b.held_long());
}

TEST(button_medium_press_is_ignored) {
  ButtonGesture b;
  uint32_t t = 0;
  drive(b, false, t, 50);
  CHECK(drive(b, true, t, 2500) == ButtonEvent::None);
  CHECK(drive(b, false, t, 100) == ButtonEvent::None);
}

TEST(button_works_across_millis_wrap) {
  ButtonGesture b;
  uint32_t t = 0xFFFFFFFFu - 2000;  // 2 s before the 32-bit wrap
  drive(b, false, t, 50);
  CHECK(drive(b, true, t, kLongHoldMs + 100) == ButtonEvent::Long);
}

TEST(modes_unprovisioned_stays_in_setup) {
  ModeMachine m;
  m.begin(false, 0);
  CHECK(m.mode() == Mode::Setup);
  CHECK(m.update(ButtonEvent::Short, 10) == ModeAction::None);
  CHECK(m.update(ButtonEvent::Long, 20) == ModeAction::None);
  CHECK(m.update(ButtonEvent::None, kMaintenanceTimeoutMs * 3) == ModeAction::None);
  CHECK(m.mode() == Mode::Setup);
}

TEST(modes_normal_maintenance_setup_path) {
  ModeMachine m;
  m.begin(true, 0);
  CHECK(m.mode() == Mode::Normal);
  CHECK(m.update(ButtonEvent::Short, 1) == ModeAction::VolumeStep);
  CHECK(m.mode() == Mode::Normal);
  CHECK(m.update(ButtonEvent::Long, 2) == ModeAction::Entered);
  CHECK(m.mode() == Mode::Maintenance);
  CHECK(m.update(ButtonEvent::Long, 3) == ModeAction::Entered);
  CHECK(m.mode() == Mode::Setup);
  CHECK(m.update(ButtonEvent::Short, 4) == ModeAction::Left);  // provisioned: may leave setup
  CHECK(m.mode() == Mode::Normal);
}

TEST(modes_maintenance_ends_by_press_or_timeout) {
  ModeMachine m;
  m.begin(true, 0);
  m.update(ButtonEvent::Long, 100);
  CHECK(m.update(ButtonEvent::Short, 200) == ModeAction::Left);
  CHECK(m.mode() == Mode::Normal);

  m.update(ButtonEvent::Long, 1000);
  CHECK(m.mode() == Mode::Maintenance);
  CHECK(m.update(ButtonEvent::None, 1000 + kMaintenanceTimeoutMs - 1) == ModeAction::None);
  m.touch_activity(1000 + kMaintenanceTimeoutMs - 1);  // activity restarts the window
  CHECK(m.update(ButtonEvent::None, 1000 + kMaintenanceTimeoutMs + 5) == ModeAction::None);
  CHECK(m.update(ButtonEvent::None, 1000 + 2 * kMaintenanceTimeoutMs) == ModeAction::Left);
  CHECK(m.mode() == Mode::Normal);
}

TEST(button_very_long_press_after_long) {
  ButtonGesture b;
  uint32_t t = 0;
  drive(b, false, t, 50);
  CHECK(drive(b, true, t, kLongHoldMs + kDebounceMs + 5) == ButtonEvent::Long);
  CHECK(drive(b, true, t, kResetHoldMs - kLongHoldMs - 20) == ButtonEvent::None);
  CHECK(drive(b, true, t, 30) == ButtonEvent::VeryLong);
  CHECK(drive(b, true, t, 20000) == ButtonEvent::None);  // only once per press
  CHECK(drive(b, false, t, 100) == ButtonEvent::None);
}

TEST(modes_factory_reset_only_from_setup) {
  ModeMachine m;
  m.begin(true, 0);
  CHECK(m.update(ButtonEvent::VeryLong, 1) == ModeAction::None);  // normal: never
  m.update(ButtonEvent::Long, 2);                                  // -> maintenance
  CHECK(m.update(ButtonEvent::VeryLong, 3) == ModeAction::None);  // maintenance: never
  m.update(ButtonEvent::Long, 4);                                  // -> setup
  CHECK(m.mode() == Mode::Setup);
  CHECK(m.update(ButtonEvent::Long, 5) == ModeAction::None);      // the 5-s mark of the same hold
  CHECK(m.update(ButtonEvent::VeryLong, 6) == ModeAction::FactoryReset);
  // an unconfigured unit can be reset too (e.g. half-finished setup)
  ModeMachine u;
  u.begin(false, 0);
  CHECK(u.update(ButtonEvent::VeryLong, 1) == ModeAction::FactoryReset);
}

TEST(modes_setup_times_out_only_when_configured) {
  ModeMachine m;
  m.begin(true, 0);
  m.update(ButtonEvent::Long, 0);
  m.update(ButtonEvent::Long, 0);
  CHECK(m.update(ButtonEvent::None, kSetupTimeoutMs - 1) == ModeAction::None);
  CHECK(m.update(ButtonEvent::None, kSetupTimeoutMs) == ModeAction::Left);
  CHECK(m.mode() == Mode::Normal);
  ModeMachine u;
  u.begin(false, 0);
  CHECK(u.update(ButtonEvent::None, kSetupTimeoutMs * 4) == ModeAction::None);  // first setup: AP stays
}

TEST(modes_maintenance_requested_by_indoor_unit) {
  ModeMachine m;
  m.begin(true, 0);
  CHECK(m.request_maintenance(10));
  CHECK(m.mode() == Mode::Maintenance);
  CHECK(m.update(ButtonEvent::None, 10 + kMaintenanceTimeoutMs) == ModeAction::Left);  // same 10-min window
  m.update(ButtonEvent::Long, 20);
  m.update(ButtonEvent::Long, 20);
  CHECK(m.mode() == Mode::Setup);
  CHECK(!m.request_maintenance(30));  // never pulls a unit out of setup
  CHECK(m.mode() == Mode::Setup);
}
