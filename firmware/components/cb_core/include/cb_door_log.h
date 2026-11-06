// cb_door_log.h - the log of door openings (Softwarearchitektur 6.4), hardware-free, PC-tested.
//
// It makes misuse visible (an opening nobody triggered) but necessarily touches BOTH households. So each unit
// keeps only what it may see:
//   indoor unit   every door command of ITS OWN flat, with time and result (opened / lock-out / refused /
//                 no answer)
//   outdoor unit  remote openings one by one (port step 9) and service openings over USB (door-test);
//                 openings requested by the indoor keys ONLY AS A SUM - no time, no flat
//
// Storage: a ring buffer of the newest kCapacity entries plus the sum, serialised into ONE NVS blob
// (main/cb_status.cpp). Time: Unix seconds once NTP has set the clock, otherwise seconds since boot
// together with the boot number - marked, so the page can say "start #57 + 1234 s".
#pragma once

#include <cstddef>
#include <cstdint>

namespace cb {

enum class DoorEvent : uint8_t {
  Opened,    // indoor: the outdoor unit confirmed the opening
  LockedOut, // indoor: refused, lock-out time of the opener still running
  Refused,   // indoor: refused (challenge invalid / not allowed)
  NoAnswer,  // indoor: no answer from the outdoor unit
  Service,   // outdoor: dry run / service pulse over the USB console (door-test)
  Remote,    // outdoor: remote opening by phone (port step 9)
  Count
};
const char *door_event_name(DoorEvent e);

struct DoorLogEntry {
  uint32_t time;   // Unix seconds (wall == true) or seconds since boot
  uint16_t boot;   // boot counter at the time of the entry
  bool wall;
  DoorEvent event;
};

class DoorLog {
 public:
  static constexpr int kCapacity = 32;
  static constexpr size_t kBlobSize = 16 + kCapacity * 8;

  void add(DoorEvent e, uint32_t time, bool wall, uint16_t boot);
  void count_local() { local_++; }  // outdoor: an opening requested by an indoor key - sum only

  int size() const { return count_; }
  // i = 0 is the newest entry, i = size()-1 the oldest.
  DoorLogEntry newest(int i) const;
  uint32_t local_count() const { return local_; }
  uint32_t total_added() const { return total_; }

  // Writes kBlobSize bytes. load() returns false (and leaves the log empty) for a damaged or foreign blob,
  // e.g. after a schema change - an empty log is better than a wrong one.
  void serialize(uint8_t out[kBlobSize]) const;
  bool load(const uint8_t *blob, size_t len);
  void clear();

 private:
  DoorLogEntry e_[kCapacity] = {};
  int head_ = 0;   // next write position
  int count_ = 0;
  uint32_t local_ = 0;
  uint32_t total_ = 0;
};

}  // namespace cb
