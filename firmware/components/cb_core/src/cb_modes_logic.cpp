// cb_modes_logic.cpp - see cb_modes_logic.h. All time arithmetic uses unsigned differences
// (now - then), which stays correct across the 49-day wrap of a 32-bit millisecond counter.
#include "cb_modes_logic.h"

namespace cb {

ButtonEvent ButtonGesture::update(bool pressed, uint32_t now_ms) {
  if (pressed != raw_last_) {
    raw_last_ = pressed;
    raw_since_ = now_ms;
  }
  ButtonEvent ev = ButtonEvent::None;
  if (pressed != stable_ && now_ms - raw_since_ >= kDebounceMs) {
    stable_ = pressed;
    if (stable_) {
      pressed_at_ = now_ms;
      long_fired_ = very_long_fired_ = false;
    } else {
      if (!long_fired_ && now_ms - pressed_at_ < kShortMaxMs) ev = ButtonEvent::Short;
      long_fired_ = false;
    }
  }
  if (stable_ && !long_fired_ && now_ms - pressed_at_ >= kLongHoldMs) {
    long_fired_ = true;
    ev = ButtonEvent::Long;
  } else if (stable_ && long_fired_ && !very_long_fired_ && now_ms - pressed_at_ >= kResetHoldMs) {
    very_long_fired_ = true;
    ev = ButtonEvent::VeryLong;
  }
  return ev;
}

bool ModeMachine::request_maintenance(uint32_t now_ms) {
  if (mode_ == Mode::Setup) return false;
  mode_ = Mode::Maintenance;
  last_activity_ = now_ms;
  return true;
}

const char *mode_name(Mode m) {
  switch (m) {
    case Mode::Normal: return "normal";
    case Mode::Maintenance: return "maintenance";
    case Mode::Setup: return "setup";
  }
  return "?";
}

void ModeMachine::begin(bool provisioned, uint32_t now_ms) {
  provisioned_ = provisioned;
  mode_ = provisioned ? Mode::Normal : Mode::Setup;
  last_activity_ = now_ms;
}

ModeAction ModeMachine::update(ButtonEvent ev, uint32_t now_ms) {
  switch (mode_) {
    case Mode::Normal:
      if (ev == ButtonEvent::Short) return ModeAction::VolumeStep;
      if (ev == ButtonEvent::Long) {
        mode_ = Mode::Maintenance;
        last_activity_ = now_ms;
        return ModeAction::Entered;
      }
      break;
    case Mode::Maintenance:
      if (ev == ButtonEvent::Short || now_ms - last_activity_ >= kMaintenanceTimeoutMs) {
        mode_ = Mode::Normal;
        return ModeAction::Left;
      }
      if (ev == ButtonEvent::Long) {
        mode_ = Mode::Setup;
        last_activity_ = now_ms;
        return ModeAction::Entered;
      }
      break;
    case Mode::Setup:
      // A provisioned unit may leave setup with a short press (nothing was changed). An unprovisioned
      // one cannot: there is no normal operation without keys. Changes take effect only after reboot.
      if (ev == ButtonEvent::VeryLong) return ModeAction::FactoryReset;  // only from setup: 3 deliberate holds
      if (provisioned_ && (ev == ButtonEvent::Short || now_ms - last_activity_ >= kSetupTimeoutMs)) {
        mode_ = Mode::Normal;
        return ModeAction::Left;
      }
      break;
  }
  return ModeAction::None;
}

}  // namespace cb
