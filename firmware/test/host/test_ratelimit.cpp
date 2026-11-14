// test_ratelimit.cpp - token bucket per source before the HMAC (cb_ratelimit.h), and the split used by cb_net since
// VK-07 (RV-37): known peers get their own, larger bucket, unknown sources share one bucket on top of their own.
#include "cb_ratelimit.h"
#include "mini_test.h"

using namespace cb;

TEST(ratelimit_default_burst_then_rate) {
  RateLimiter l;
  int ok = 0;
  for (int i = 0; i < 100; i++) ok += l.allow(0x0A000001, 1000);  // 100 frames in the same millisecond
  CHECK_EQ(ok, (int)RateLimiter::kBurst);
  CHECK(!l.allow(0x0A000001, 1050));  // 50 ms = half a token: still empty
  CHECK(l.allow(0x0A000001, 1100));   // 100 ms = one token at 10/s
  CHECK(l.allow(0x0A000002, 1100));   // another source has its own bucket
}

TEST(ratelimit_configurable_for_peers) {
  RateLimiter peers(40, 50);  // the peer bucket of cb_net: a spoofer needs far more traffic to empty it
  int ok = 0;
  for (int i = 0; i < 100; i++) ok += peers.allow(0x0A000001, 1000);
  CHECK_EQ(ok, 40);
  CHECK(peers.allow(0x0A000001, 1020));  // 20 ms at 50/s = one token
  RateLimiter l;
  for (int i = 0; i < 100; i++) l.allow(0x0A000001, 1000);
  CHECK(!l.allow(0x0A000001, 1020));  // the default bucket would still be empty
}

// RV-37: more than kSources rotating fake addresses used to get a fresh bucket each. One shared bucket for all
// unknown sources (keyed by a constant) caps their sum.
TEST(ratelimit_shared_bucket_caps_rotating_sources) {
  RateLimiter per_source, unknown_total(20, 20);
  int passed = 0;
  for (uint32_t i = 0; i < 1000; i++) {
    uint32_t src = 0xC0A80000 + i;  // 1000 different fake senders within one second
    uint32_t now = 1000 + i;
    if (per_source.allow(src, now) && unknown_total.allow(0, now)) passed++;
  }
  CHECK(passed <= 20 + 20 + 1);  // burst + one second of refill
  CHECK(passed >= 20);
}
