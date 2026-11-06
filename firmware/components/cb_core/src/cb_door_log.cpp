// cb_door_log.cpp - see cb_door_log.h.
//
// Blob layout (little endian), version 1:
//   0  'D' 'L' version count head  0 0 0     8  local sum (u32)   12  total entries ever (u32)
//   16 + 8*i: time (u32), boot (u16), flags (u8: bit0 = wall clock), event (u8)
#include "cb_door_log.h"

#include <cstring>

#include "cb_bytes.h"

namespace cb {

namespace {
constexpr uint8_t kVersion = 1;
}

const char *door_event_name(DoorEvent e) {
  switch (e) {
    case DoorEvent::Opened: return "opened";
    case DoorEvent::LockedOut: return "lock-out";
    case DoorEvent::Refused: return "refused";
    case DoorEvent::NoAnswer: return "no answer";
    case DoorEvent::Service: return "service";
    case DoorEvent::Remote: return "remote";
    default: return "?";
  }
}

void DoorLog::add(DoorEvent e, uint32_t time, bool wall, uint16_t boot) {
  e_[head_] = {time, boot, wall, e};
  head_ = (head_ + 1) % kCapacity;
  if (count_ < kCapacity) count_++;
  total_++;
}

DoorLogEntry DoorLog::newest(int i) const {
  if (i < 0 || i >= count_) return {};
  return e_[(head_ - 1 - i + 2 * kCapacity) % kCapacity];
}

void DoorLog::serialize(uint8_t out[kBlobSize]) const {
  memset(out, 0, kBlobSize);
  out[0] = 'D';
  out[1] = 'L';
  out[2] = kVersion;
  out[3] = (uint8_t)count_;
  out[4] = (uint8_t)head_;
  put_u32le(out + 8, local_);
  put_u32le(out + 12, total_);
  for (int i = 0; i < kCapacity; i++) {
    uint8_t *p = out + 16 + 8 * i;
    put_u32le(p, e_[i].time);
    put_u16le(p + 4, e_[i].boot);
    p[6] = e_[i].wall ? 1 : 0;
    p[7] = (uint8_t)e_[i].event;
  }
}

bool DoorLog::load(const uint8_t *b, size_t len) {
  clear();
  if (!b || len != kBlobSize || b[0] != 'D' || b[1] != 'L' || b[2] != kVersion) return false;
  if (b[3] > kCapacity || b[4] >= kCapacity) return false;
  DoorLog tmp;
  tmp.count_ = b[3];
  tmp.head_ = b[4];
  tmp.local_ = get_u32le(b + 8);
  tmp.total_ = get_u32le(b + 12);
  for (int i = 0; i < kCapacity; i++) {
    const uint8_t *p = b + 16 + 8 * i;
    if (p[6] > 1 || p[7] >= (uint8_t)DoorEvent::Count) return false;
    tmp.e_[i] = {get_u32le(p), get_u16le(p + 4), p[6] == 1, (DoorEvent)p[7]};
  }
  // A partly filled log has always been written from index 0 on.
  if (tmp.count_ < kCapacity && tmp.head_ != tmp.count_) return false;
  *this = tmp;
  return true;
}

void DoorLog::clear() {
  *this = DoorLog();
}

}  // namespace cb
