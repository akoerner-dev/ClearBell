// cb_ratelimit.cpp - see cb_ratelimit.h.
#include "cb_ratelimit.h"

namespace cb {

bool RateLimiter::allow(uint32_t source, uint32_t now_ms) {
  Entry *e = nullptr;
  for (Entry &x : e_)
    if (x.used && x.source == source) e = &x;
  if (!e) {
    // New source: take a free entry, else the one heard from longest ago.
    e = &e_[0];
    for (Entry &x : e_) {
      if (!x.used) {
        e = &x;
        break;
      }
      if (now_ms - x.last_ms > now_ms - e->last_ms) e = &x;
    }
    e->used = true;
    e->source = source;
    e->tokens_milli = burst_ * 1000;
    e->last_ms = now_ms;
  }
  uint32_t elapsed = now_ms - e->last_ms;
  e->last_ms = now_ms;
  // Refill: per_second_ tokens per 1000 ms = per_second_ milli-tokens per ms. Cap elapsed to avoid overflow.
  if (elapsed > 10000) elapsed = 10000;
  e->tokens_milli += elapsed * per_second_;
  if (e->tokens_milli > burst_ * 1000) e->tokens_milli = burst_ * 1000;
  if (e->tokens_milli < 1000) return false;
  e->tokens_milli -= 1000;
  return true;
}

}  // namespace cb
