// cb_modes_logic.h - boot-button gestures and the operating-mode state machine, hardware-free.
//
// Operating modes (Softwarearchitektur 5.9; hold times and windows are draft values):
//
//   Normal --hold 5 s--> Maintenance --hold 5 s--> Setup --hold 15 s--> factory reset
//   Maintenance --short press, or 10 min without action--> Normal
//   Setup (configured unit) --short press, or 30 min without action--> Normal
//   start without WiFi configuration --> Setup (left only by configuring + reboot)
//
//   Normal       ringing, door command, remote opening; status page read-only
//   Maintenance  additionally: tone upload, update, settings
//   Setup        setup access point + setup page (WiFi, flat), pairing, factory reset (port step 10a)
//
// The boot button is inside the housing: physical presence is the second factor for everything that
// changes the firmware or its secrets. A short press in Normal steps the volume (as in V0.1).
//
// The Arduino side (main/cb_modes.cpp) feeds the raw button level and the time in; everything that
// decides lives here and is unit-tested on the PC (test/host/test_modes.cpp).
#pragma once

#include <cstdint>

namespace cb {

constexpr uint32_t kDebounceMs = 30;        // contact bounce of the tactile switch
constexpr uint32_t kShortMaxMs = 1000;      // released before this: short press
constexpr uint32_t kLongHoldMs = 5000;      // held this long: long press (fires while still held)
constexpr uint32_t kResetHoldMs = 15000;    // held this long IN SETUP: factory reset (the buyer's data only)
constexpr uint32_t kMaintenanceTimeoutMs = 10UL * 60 * 1000;
constexpr uint32_t kSetupTimeoutMs = 30UL * 60 * 1000;  // setup access point closes (configured unit only)

enum class ButtonEvent : uint8_t { None, Short, Long, VeryLong };

// Turns the sampled button level into gestures. Press 1-5 s and release = nothing (ambiguous, ignored).
// Holding on: Long at 5 s, then VeryLong at 15 s (both while still held).
class ButtonGesture {
 public:
  ButtonEvent update(bool pressed, uint32_t now_ms);
  bool held_long() const { return long_fired_; }  // true from the Long event until release

 private:
  bool stable_ = false;       // debounced level
  bool raw_last_ = false;
  uint32_t raw_since_ = 0;    // when the raw level last changed
  uint32_t pressed_at_ = 0;
  bool long_fired_ = false;
  bool very_long_fired_ = false;
};

enum class Mode : uint8_t { Normal, Maintenance, Setup };

const char *mode_name(Mode m);

// What the mode machine asks the application to do as a side effect of an event.
enum class ModeAction : uint8_t { None, VolumeStep, Entered, Left, FactoryReset };

class ModeMachine {
 public:
  // `provisioned`: factory data valid for this image. Without it the machine stays in Setup forever.
  void begin(bool provisioned, uint32_t now_ms);
  // Feed one button event (or None) per loop pass. Returns the side effect to perform.
  ModeAction update(ButtonEvent ev, uint32_t now_ms);
  // Anything the user does in maintenance (console command, upload) restarts the 10-minute window.
  void touch_activity(uint32_t now_ms) { last_activity_ = now_ms; }
  // Outdoor unit: a paired indoor unit asked for maintenance (someone pressed its button, port step 10b). Only from
  // Normal; never leaves Setup. Returns true if maintenance was entered (or is already open, window restarted).
  bool request_maintenance(uint32_t now_ms);
  Mode mode() const { return mode_; }

 private:
  Mode mode_ = Mode::Setup;
  bool provisioned_ = false;
  uint32_t last_activity_ = 0;
};

}  // namespace cb
