// cb_http_guard.cpp - see cb_http_guard.h.
#include "cb_http_guard.h"

#include <cctype>
#include <cstdio>
#include <cstring>

namespace cb {

int BodyReader::read(char *buf, size_t n) {
  while (!gave_up_) {
    if (now_() - start_ > total_ms_) break;  // total deadline: also catches a sender that trickles single bytes
    int got = recv_(ctx_, buf, n);
    if (got > 0) {
      timeouts_ = 0;
      return got;
    }
    if (got != kTimeout || ++timeouts_ >= kTimeoutsInRow) break;
  }
  gave_up_ = true;
  return 0;
}

bool read_full(BodyReader &r, char *buf, size_t len) {
  size_t got = 0;
  while (got < len) {
    int n = r.read(buf + got, len - got);
    if (n <= 0) return false;
    got += (size_t)n;
  }
  return true;
}

// ---- FailGuard ------------------------------------------------------------------------------------------------

bool FailGuard::locked(uint32_t source, uint32_t now_ms) const {
  if (global_started_ && global_fails_ >= kGlobalMax && now_ms - global_since_ < kGlobalWindowMs) return true;
  for (const Entry &e : e_)
    if (e.used && e.source == source && e.locked_until && (int32_t)(e.locked_until - now_ms) > 0) return true;
  return false;
}

void FailGuard::fail(uint32_t source, uint32_t now_ms) {
  if (!global_started_ || now_ms - global_since_ >= kGlobalWindowMs) {
    global_started_ = true;
    global_since_ = now_ms;
    global_fails_ = 0;
  }
  global_fails_++;
  Entry *slot = nullptr, *oldest = &e_[0];
  for (Entry &e : e_) {
    if (e.used && e.source == source) slot = &e;
    if (!e.used || (oldest->used && (int32_t)(e.last - oldest->last) < 0)) oldest = &e;
  }
  if (!slot) {
    slot = oldest;
    *slot = Entry{};
    slot->used = true;
    slot->source = source;
  }
  slot->last = now_ms;
  if (slot->locked_until && (int32_t)(slot->locked_until - now_ms) <= 0) {  // an expired lock starts anew
    slot->locked_until = 0;
    slot->fails = 0;
  }
  if (++slot->fails >= kMaxFails) slot->locked_until = (now_ms + kLockMs) | 1;  // never 0 = "not locked"
}

void FailGuard::success(uint32_t source) {
  for (Entry &e : e_)
    if (e.used && e.source == source) e = Entry{};
}

// ---- Cross-site request check ---------------------------------------------------------------------------------

namespace {
// Host header without port, compared case-insensitively.
bool host_is(const char *host, const char *want) {
  size_t n = strlen(want);
  for (size_t i = 0; i < n; i++)
    if (!host[i] || tolower((unsigned char)host[i]) != tolower((unsigned char)want[i])) return false;
  return host[n] == 0 || host[n] == ':';
}

bool host_has_prefix(const char *host, const char *prefix) {
  size_t n = strlen(prefix);
  for (size_t i = 0; i < n; i++)
    if (!host[i] || tolower((unsigned char)host[i]) != tolower((unsigned char)prefix[i])) return false;
  return true;
}
}  // namespace

bool request_origin_ok(const char *method, const char *origin, const char *sec_fetch_site, const char *x_clearbell,
                       const char *host, const char *serial, const char *own_ip, bool via_ap) {
  if (!method || !strcmp(method, "GET") || !strcmp(method, "HEAD")) return true;  // reading changes nothing
  if (!x_clearbell || strcmp(x_clearbell, "1") != 0) return false;
  if (sec_fetch_site && *sec_fetch_site && strcmp(sec_fetch_site, "same-origin") != 0 && strcmp(sec_fetch_site, "none") != 0)
    return false;
  if (!host || !*host) return false;
  char name[40];
  snprintf(name, sizeof name, "clearbell-%s", serial ? serial : "");
  bool own = (own_ip && *own_ip && host_is(host, own_ip)) || (via_ap && host_is(host, "192.168.4.1")) ||
             host_is(host, name);
  if (!own) {  // clearbell-<serial>.local or a router domain like clearbell-<serial>.fritz.box
    char dotted[44];
    snprintf(dotted, sizeof dotted, "%s.", name);
    own = host_has_prefix(host, dotted);
  }
  if (!own) return false;
  if (origin && *origin) {  // the Origin must be this very host
    if (strncmp(origin, "http://", 7) != 0) return false;
    const char *o = origin + 7;
    size_t hl = strcspn(host, ":"), ol = strcspn(o, ":/");
    if (hl != ol) return false;
    for (size_t i = 0; i < hl; i++)
      if (tolower((unsigned char)o[i]) != tolower((unsigned char)host[i])) return false;
  }
  return true;
}

}  // namespace cb
