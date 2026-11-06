// cb_door_logic.h - lock-out and pulse limits of the door opener, hardware-free (Softwarearchitektur 5.4).
// The device side (main/cb_door.cpp) asks this object before every opening, whatever the path.
#pragma once

#include <cstdint>

namespace cb {

// Draft values - pulse length F-3 is decided with the real opener (effeff 118W); the hard maximum stays.
constexpr uint32_t kDoorPulseMs = 2000;      // default pulse (V0.1 start value)
constexpr uint32_t kDoorPulseMinMs = 100;
constexpr uint32_t kDoorPulseMaxMs = 5000;   // hard upper bound of the on-time, also for the dry-run command
constexpr uint32_t kDoorLockoutMs = 5000;    // after the START of an opening, for ALL paths

class DoorGuard {
 public:
  // May an opening start now? (Does not record anything - see opened().)
  bool allowed(uint32_t now_ms) const { return !any_ || now_ms - last_ >= kDoorLockoutMs; }
  // Record that an opening started now.
  void opened(uint32_t now_ms) {
    any_ = true;
    last_ = now_ms;
    count_++;
  }
  uint32_t count() const { return count_; }
  // Clamps a requested pulse length into [kDoorPulseMinMs, kDoorPulseMaxMs].
  static uint32_t clamp_pulse(uint32_t ms) {
    return ms < kDoorPulseMinMs ? kDoorPulseMinMs : ms > kDoorPulseMaxMs ? kDoorPulseMaxMs : ms;
  }

 private:
  bool any_ = false;
  uint32_t last_ = 0, count_ = 0;
};

}  // namespace cb
