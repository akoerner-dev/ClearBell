// cb_ratelimit.h - rate limit per source address, applied BEFORE the HMAC check (Softwarearchitektur 4.2:
// "Länge und magic -> Ratenbegrenzung je Absender -> Version -> Tag").
//
// Why before the HMAC: checking a tag costs time; a device in the WiFi that floods the port with garbage
// must not be able to keep the unit busy. Each source IPv4 address gets a token bucket: kBurst frames at
// once, then kPerSecond frames per second. Normal traffic between two units is a few frames per ring and
// one ping per 30 s - far below. The table holds kSources addresses; a new one replaces the least
// recently seen.
#pragma once

#include <cstdint>

namespace cb {

class RateLimiter {
 public:
  static constexpr int kSources = 8;
  static constexpr uint32_t kBurst = 20;
  static constexpr uint32_t kPerSecond = 10;

  // True = let the frame through; false = drop it (counted by the caller).
  bool allow(uint32_t source, uint32_t now_ms);

 private:
  struct Entry {
    uint32_t source = 0;
    uint32_t tokens_milli = 0;  // tokens x 1000, so refill needs no floating point
    uint32_t last_ms = 0;
    bool used = false;
  };
  Entry e_[kSources];
};

}  // namespace cb
