// cb_http_guard.h - protections of the web server that need no hardware (VK-06_Webserver_Robustheit, security
// catalogue 13.11.2026). PC-tested in test/host/test_http_guard.cpp; the device glue is main/cb_http_util.
//
// BodyReader (RV-17): the ESP web server has ONE task. A client that announces a body and then sends nothing (or
// one byte every few seconds) used to hold that task forever - the receive timeout was answered with "continue".
// Now: two receive timeouts in a row (2 x 10 s) or the total deadline end the read.
//
// FailGuard (RV-41, RV-35 part B): wrong pairing offers used to count globally - five garbage offers from ANY
// device in the WiFi locked pairing for 10 min. Now per source address; a high global brake stays against
// attackers that rotate addresses. Against guessing the 80-bit device code a lock is not needed at all.
//
// request_origin_ok (RV-39): browsers attach cached HTTP-auth data to form posts from foreign pages. A writing
// request (POST/PUT/DELETE) is only accepted with the header "X-ClearBell: 1" (a foreign page cannot set it without a
// CORS preflight, which the unit never answers), a same-origin Fetch-Metadata/Origin, and a Host that is this unit.
#pragma once

#include <cstddef>
#include <cstdint>

namespace cb {

class BodyReader {
 public:
  using RecvFn = int (*)(void *ctx, char *buf, size_t len);  // > 0 bytes, kTimeout, <= 0 error/closed
  using NowFn = uint32_t (*)();
  static constexpr int kTimeout = -3;  // = HTTPD_SOCK_ERR_TIMEOUT of esp_http_server
  static constexpr int kTimeoutsInRow = 2;

  BodyReader(RecvFn recv, void *ctx, NowFn now, uint32_t total_ms)
      : recv_(recv), ctx_(ctx), now_(now), start_(now()), total_ms_(total_ms) {}
  // Up to n bytes; > 0 = bytes read, 0 = gave up (silent sender, deadline, socket error) - see gave_up().
  int read(char *buf, size_t n);
  bool gave_up() const { return gave_up_; }

 private:
  RecvFn recv_;
  void *ctx_;
  NowFn now_;
  uint32_t start_, total_ms_;
  int timeouts_ = 0;
  bool gave_up_ = false;
};

// Reads exactly len bytes (a whole small body). False = incomplete.
bool read_full(BodyReader &r, char *buf, size_t len);

class FailGuard {
 public:
  static constexpr int kSources = 8;
  static constexpr int kMaxFails = 5;
  static constexpr uint32_t kLockMs = 5UL * 60 * 1000;
  static constexpr int kGlobalMax = 100;
  static constexpr uint32_t kGlobalWindowMs = 10UL * 60 * 1000;

  bool locked(uint32_t source, uint32_t now_ms) const;
  void fail(uint32_t source, uint32_t now_ms);
  void success(uint32_t source);

 private:
  struct Entry {
    uint32_t source = 0;
    int fails = 0;
    uint32_t locked_until = 0;
    uint32_t last = 0;
    bool used = false;
  };
  Entry e_[kSources];
  int global_fails_ = 0;
  uint32_t global_since_ = 0;
  bool global_started_ = false;
};

// method: "GET", "POST" ...; origin, sec_fetch_site, x_clearbell, host: request headers, nullptr = missing.
// serial: unit number (host "clearbell-<serial>.local" or "clearbell-<serial>.<router domain>"), own_ip: dotted
// station address, via_ap: the request came in over the setup access point (host 192.168.4.1 allowed).
bool request_origin_ok(const char *method, const char *origin, const char *sec_fetch_site, const char *x_clearbell,
                       const char *host, const char *serial, const char *own_ip, bool via_ap);

}  // namespace cb
