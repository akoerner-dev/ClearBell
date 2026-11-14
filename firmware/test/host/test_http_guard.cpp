// test_http_guard.cpp - VK-06_Webserver_Robustheit (security catalogue 13.11.2026): bounded body reads (RV-17),
// failed attempts per source (RV-41, RV-35 part B), cross-site request check (RV-39). cb_http_guard.h.
#include <cstring>
#include <vector>

#include "cb_http_guard.h"
#include "mini_test.h"

using namespace cb;

namespace {

// A fake socket: a script of recv results, each "advances" the clock by its delay.
struct Script {
  struct Step {
    int result;  // > 0 bytes, BodyReader::kTimeout, or <= 0 error
    uint32_t delay_ms;
  };
  std::vector<Step> steps;
  size_t pos = 0;
};
uint32_t g_now = 0;
uint32_t fake_now() { return g_now; }
int fake_recv(void *ctx, char *buf, size_t len) {
  Script *s = static_cast<Script *>(ctx);
  if (s->pos >= s->steps.size()) {
    g_now += 10000;  // an idle socket: every call is a 10 s receive timeout
    return BodyReader::kTimeout;
  }
  Script::Step st = s->steps[s->pos++];
  g_now += st.delay_ms;
  if (st.result > 0) {
    size_t n = (size_t)st.result < len ? (size_t)st.result : len;
    memset(buf, 'x', n);
    return (int)n;
  }
  return st.result;
}

}  // namespace

TEST(body_reader_reads_a_normal_body) {
  g_now = 0;
  Script s{{{40, 5}, {60, 5}}};
  char buf[128];
  BodyReader r(fake_recv, &s, fake_now, 15000);
  CHECK(read_full(r, buf, 100));
  CHECK(!r.gave_up());
}

TEST(body_reader_gives_up_on_a_silent_sender) {
  // Content-Length announced, then nothing: two receive timeouts in a row end it (RV-17: before, the single
  // web server task waited forever).
  g_now = 0;
  Script s{{{10, 1}}};
  char buf[400];
  BodyReader r(fake_recv, &s, fake_now, 15000);
  CHECK(!read_full(r, buf, 399));
  CHECK(r.gave_up());
  CHECK(g_now <= 20001 + 1);  // after ~20 s, not later
}

TEST(body_reader_one_slow_timeout_is_tolerated) {
  g_now = 0;
  Script s{{{10, 1}, {BodyReader::kTimeout, 10000}, {90, 5}}};
  char buf[128];
  BodyReader r(fake_recv, &s, fake_now, 30000);
  CHECK(read_full(r, buf, 100));  // one stall of 10 s is a slow phone, not an attack
}

TEST(body_reader_total_deadline_against_trickling) {
  // 1 byte every 900 ms never triggers the "two timeouts" rule - the total deadline does.
  g_now = 0;
  Script s;
  for (int i = 0; i < 200; i++) s.steps.push_back({1, 900});
  char buf[256];
  BodyReader r(fake_recv, &s, fake_now, 15000);
  CHECK(!read_full(r, buf, 200));
  CHECK(r.gave_up());
  CHECK(g_now <= 15000 + 900);
}

TEST(body_reader_socket_error_ends_at_once) {
  g_now = 0;
  Script s{{{10, 1}, {-1, 1}}};
  char buf[64];
  BodyReader r(fake_recv, &s, fake_now, 15000);
  CHECK(!read_full(r, buf, 50));
}

TEST(fail_guard_locks_only_the_guessing_source) {
  FailGuard g;  // 5 failures -> that source waits
  const uint32_t A = 0xC0A80A01, B = 0xC0A80A02;
  for (int i = 0; i < 5; i++) {
    CHECK(!g.locked(A, 1000));
    g.fail(A, 1000);
  }
  CHECK(g.locked(A, 1001));
  CHECK(!g.locked(B, 1001));  // RV-41: another device in the WiFi is NOT locked out with it
  CHECK(!g.locked(A, 1001 + FailGuard::kLockMs));  // the lock ends
}

TEST(fail_guard_success_clears_and_table_rotates) {
  FailGuard g;
  const uint32_t A = 0xC0A80A01;
  for (int i = 0; i < 4; i++) g.fail(A, 1000);
  g.success(A);
  for (int i = 0; i < 4; i++) g.fail(A, 1000);
  CHECK(!g.locked(A, 1000));  // the count started anew
  for (uint32_t i = 0; i < 20; i++) g.fail(0x0A000000 + i, 2000);  // many other sources push A out of the table
  CHECK(!g.locked(A, 2000));
}

TEST(fail_guard_global_brake_against_rotating_addresses) {
  FailGuard g;
  int i = 0;
  for (; i < 1000 && !g.locked(0x0A000000u + (uint32_t)i, 5000); i++) g.fail(0x0A000000u + (uint32_t)i, 5000);
  CHECK_EQ(i, FailGuard::kGlobalMax);  // after the global maximum every source waits ...
  CHECK(g.locked(0x0BADBEEF, 5000));
  CHECK(!g.locked(0x0BADBEEF, 5000 + FailGuard::kGlobalWindowMs));  // ... until the window has passed
}

TEST(origin_check_allows_own_page_and_tools) {
  const char *ser = "a1b2c3", *ip = "192.168.10.20";
  CHECK(request_origin_ok("GET", nullptr, nullptr, nullptr, "evil.example", ser, ip, false));  // reading: always
  CHECK(request_origin_ok("POST", "http://clearbell-a1b2c3.local", "same-origin", "1", "clearbell-a1b2c3.local", ser,
                          ip, false));
  CHECK(request_origin_ok("POST", nullptr, nullptr, "1", "192.168.10.20", ser, ip, false));  // PC tool: no Origin
  CHECK(request_origin_ok("PUT", "http://192.168.10.20", "same-origin", "1", "192.168.10.20:80", ser, ip, false));
  CHECK(request_origin_ok("POST", nullptr, "none", "1", "CLEARBELL-A1B2C3.LOCAL", ser, ip, false));  // case
  CHECK(request_origin_ok("POST", nullptr, nullptr, "1", "clearbell-a1b2c3.fritz.box", ser, ip, false));  // router DNS
  CHECK(request_origin_ok("POST", "http://192.168.4.1", "same-origin", "1", "192.168.4.1", ser, ip, true));  // setup AP
}

TEST(origin_check_refuses_cross_site_requests) {
  const char *ser = "a1b2c3", *ip = "192.168.10.20";
  CHECK(!request_origin_ok("POST", nullptr, nullptr, nullptr, "clearbell-a1b2c3.local", ser, ip, false));  // no header
  CHECK(!request_origin_ok("POST", "http://evil.example", "cross-site", "1", "clearbell-a1b2c3.local", ser, ip, false));
  CHECK(!request_origin_ok("POST", "http://evil.example", nullptr, "1", "clearbell-a1b2c3.local", ser, ip, false));
  CHECK(!request_origin_ok("POST", nullptr, "same-site", "1", "clearbell-a1b2c3.local", ser, ip, false));
  CHECK(!request_origin_ok("POST", nullptr, nullptr, "1", "evil.example", ser, ip, false));  // DNS rebinding
  CHECK(!request_origin_ok("POST", nullptr, nullptr, "1", "192.168.4.1", ser, ip, false));  // AP address only via AP
  CHECK(!request_origin_ok("DELETE", nullptr, nullptr, "0", "192.168.10.20", ser, ip, false));
}
