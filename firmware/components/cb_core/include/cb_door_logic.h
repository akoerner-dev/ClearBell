// cb_door_logic.h - lock-out and pulse limits of the door opener, hardware-free (Softwarearchitektur 5.4).
// The device side (main/cb_door.cpp) asks this object before every opening, whatever the path.
#pragma once

#include <cstdint>

namespace cb {

// Pulse length F-3 fixed at 2000 ms (07.11.2026, effeff 118W at 12 V / 270 mA, 10/10 switched). The lock-out is
// still a draft; the hard maximum stays. Buyers with another opener set their own length on the web page (10b).
constexpr uint32_t kDoorPulseMs = 2000;      // default pulse (F-3)
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
