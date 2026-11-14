// test_status.cpp - port step 8: device password (Digest), door log, JSON writer.
#include <cstdio>
#include <cstring>

#include "cb_door_log.h"
#include "cb_http_auth.h"
#include "cb_json.h"
#include "mini_test.h"

using namespace cb;

// ---- MD5 and Digest against the published vectors ----------------------------------------------------------

TEST(md5_rfc1321_vectors) {
  struct {
    const char *in, *out;
  } v[] = {{"", "d41d8cd98f00b204e9800998ecf8427e"},
           {"a", "0cc175b9c0f1b6a831c399e269772661"},
           {"abc", "900150983cd24fb0d6963f7d28e17f72"},
           {"message digest", "f96b697d7cb7938d525a2f31aaf161d0"},
           {"12345678901234567890123456789012345678901234567890123456789012345678901234567890",
            "57edf4a22be3c955ac49da2e2107b67a"}};
  for (auto &t : v) {
    char h[33];
    md5_hex(t.in, h);
    CHECK(strcmp(h, t.out) == 0);
  }
}

TEST(digest_rfc7616_example) {
  // RFC 7616 section 3.9.1, MD5 variant (cross-checked with Python hashlib).
  const char *hdr =
      "Digest username=\"Mufasa\", realm=\"http-auth@example.org\", uri=\"/dir/index.html\", "
      "algorithm=MD5, nonce=\"7ypf/xlj9XXwfDPEoM4URrv/xwf94BcCAzFZH4GiTo0v\", nc=00000001, "
      "cnonce=\"f2/wE4q74E6zIJEtWaHKaf5wv/H5QzzpXusqGemxURZJ\", qop=auth, "
      "response=\"8ca523f5e9506fed4657c9700eebdbec\", opaque=\"FQhe/qaU925kfnzjCev0ciny7QMkPqMAFRtzCUYo5tdS\"";
  DigestFields f;
  CHECK(parse_digest(hdr, &f));
  CHECK(strcmp(f.username, "Mufasa") == 0);
  CHECK(strcmp(f.nc, "00000001") == 0);
  char r[33];
  digest_response(f, "Circle of Life", "GET", r);
  CHECK(strcmp(r, "8ca523f5e9506fed4657c9700eebdbec") == 0);
}

TEST(digest_parser_rejects_bad_headers) {
  DigestFields f;
  CHECK(!parse_digest(nullptr, &f));
  CHECK(!parse_digest("Basic dXNlcjpwYXNz", &f));
  CHECK(!parse_digest("Digest username=\"a\"", &f));  // mandatory fields missing
  // qop missing (RFC 2069 mode without nonce count) -> refused
  CHECK(!parse_digest("Digest username=\"a\", nonce=\"n\", uri=\"/\", response=\"r\", cnonce=\"c\", nc=1", &f));
  // SHA-256 not offered by us
  CHECK(!parse_digest(
      "Digest username=\"a\", nonce=\"n\", uri=\"/\", response=\"r\", cnonce=\"c\", nc=1, qop=auth, algorithm=SHA-256",
      &f));
  // unterminated quote, escapes, overlong field
  CHECK(!parse_digest("Digest username=\"a, nonce=n", &f));
  CHECK(!parse_digest("Digest username=\"a\\\"b\", nonce=\"n\", uri=\"/\", response=\"r\", cnonce=\"c\", nc=1, qop=auth", &f));
  char longhdr[600];
  snprintf(longhdr, sizeof longhdr,
           "Digest username=\"%060d\", nonce=\"n\", uri=\"/\", response=\"r\", cnonce=\"c\", nc=1, qop=auth", 0);
  CHECK(!parse_digest(longhdr, &f));
  // garbage must never crash
  const char *junk[] = {"Digest ", "Digest =", "Digest a", "Digest a=", "Digest ,,,,", "Digest a=\"", "Digest \"\"=1"};
  for (const char *j : junk) parse_digest(j, &f);
  CHECK(true);
}

// ---- Server flow, with a client that computes like a browser -------------------------------------------------

namespace {

const uint8_t kSecret[32] = {1, 2, 3};

// Extracts the nonce from a challenge and builds the Authorization header a browser would send.
void client_header(const char *challenge, const char *user, const char *pass, const char *method,
                   const char *uri, unsigned nc, char *out, size_t n) {
  const char *p = strstr(challenge, "nonce=\"");
  char nonce[48] = {};
  if (p) {
    p += 7;
    const char *e = strchr(p, '"');
    memcpy(nonce, p, e && e - p < 47 ? (size_t)(e - p) : 0);
  }
  DigestFields f{};
  snprintf(f.username, sizeof f.username, "%s", user);
  snprintf(f.realm, sizeof f.realm, "ClearBell test");
  snprintf(f.nonce, sizeof f.nonce, "%s", nonce);
  snprintf(f.uri, sizeof f.uri, "%s", uri);
  snprintf(f.qop, sizeof f.qop, "auth");
  snprintf(f.nc, sizeof f.nc, "%08x", nc);
  snprintf(f.cnonce, sizeof f.cnonce, "0a4f113b");
  char resp[33];
  digest_response(f, pass, method, resp);
  snprintf(out, n,
           "Digest username=\"%s\", realm=\"%s\", nonce=\"%s\", uri=\"%s\", algorithm=MD5, response=\"%s\", "
           "qop=auth, nc=%s, cnonce=\"%s\"",
           user, f.realm, nonce, uri, resp, f.nc, f.cnonce);
}

DigestAuth make_auth() {
  DigestAuth a;
  a.begin(kSecret, "ClearBell test", "clearbell", "geheim-123");
  return a;
}

// invented sender addresses 10.77.0.x
constexpr uint32_t src(unsigned x) { return 0x0A4D0000u + x; }
constexpr uint32_t A = 0x0A4D0001u, B = 0x0A4D0002u;

}  // namespace

TEST(auth_happy_path_and_nonce_count) {
  DigestAuth a = make_auth();
  char ch[200], h[400];
  CHECK(a.check(nullptr, "GET", "/", 100, A) == AuthResult::Missing);
  a.challenge(100, false, ch, sizeof ch);
  CHECK(strstr(ch, "qop=\"auth\"") != nullptr);
  CHECK(strstr(ch, "stale") == nullptr);
  client_header(ch, "clearbell", "geheim-123", "GET", "/", 1, h, sizeof h);
  CHECK(a.check(h, "GET", "/", 101, A) == AuthResult::Ok);
  // the browser re-uses the nonce with a growing count: fine
  client_header(ch, "clearbell", "geheim-123", "GET", "/status.json", 2, h, sizeof h);
  CHECK(a.check(h, "GET", "/status.json", 102, A) == AuthResult::Ok);
  // replaying the same header (count does not grow): refused
  CHECK(a.check(h, "GET", "/status.json", 103, A) == AuthResult::Replay);
  // a recorded header used for another URI: refused
  client_header(ch, "clearbell", "geheim-123", "GET", "/status.json", 3, h, sizeof h);
  CHECK(a.check(h, "PUT", "/ton/ring", 104, A) != AuthResult::Ok);
  CHECK(a.check(h, "GET", "/other", 104, A) == AuthResult::WrongUri);
  CHECK_EQ(a.fail_count(), 1u);  // PUT with the GET answer = wrong response -> counted
}

// RV-15: the lock is per source address (was: for the whole unit, so one device in the WiFi locked everybody out).
TEST(auth_wrong_password_locks_only_that_source) {
  DigestAuth a = make_auth();
  char ch[200], h[400];
  a.challenge(10, false, ch, sizeof ch);
  for (int i = 0; i < DigestAuth::kMaxFailures; i++) {
    client_header(ch, "clearbell", "falsch", "GET", "/", (unsigned)(i + 1), h, sizeof h);
    CHECK(a.check(h, "GET", "/", 11, A) == AuthResult::Bad);
  }
  // A is locked: even the right password is not checked, and the remaining time is reported
  client_header(ch, "clearbell", "geheim-123", "GET", "/", 10, h, sizeof h);
  CHECK(a.check(h, "GET", "/", 12, A) == AuthResult::LockedOut);
  CHECK_EQ(a.retry_after_s(12, A), DigestAuth::kLockS - 1);
  CHECK(a.check(h, "GET", "/", 11 + DigestAuth::kLockS - 1, A) == AuthResult::LockedOut);
  // B (another device) with the right password gets in meanwhile
  CHECK(a.check(h, "GET", "/", 12, B) == AuthResult::Ok);
  CHECK(!a.locked(12, B));
  // the lock ends for A
  client_header(ch, "clearbell", "geheim-123", "GET", "/", 11, h, sizeof h);
  CHECK(a.check(h, "GET", "/", 11 + DigestAuth::kLockS, A) == AuthResult::Ok);
  CHECK_EQ(a.lock_count(), 1u);
  // wrong user name counts like a wrong password
  client_header(ch, "admin", "geheim-123", "GET", "/", 12, h, sizeof h);
  CHECK(a.check(h, "GET", "/", 50, A) == AuthResult::Bad);
}

namespace {
// n wrong passwords from `who` at time t (fresh nonce counts); returns the last result
AuthResult wrong_tries(DigestAuth &a, uint32_t who, int n, uint32_t t, unsigned &nc) {
  char ch[200], h[400];
  a.challenge(t, false, ch, sizeof ch);
  AuthResult r = AuthResult::Missing;
  for (int i = 0; i < n; i++) {
    client_header(ch, "clearbell", "falsch", "GET", "/", nc++, h, sizeof h);
    r = a.check(h, "GET", "/", t, who);
  }
  return r;
}
}  // namespace

// A header that cannot be parsed and a wrong realm mean the password was not checked: nobody gets locked.
TEST(auth_malformed_header_and_wrong_realm_do_not_count) {
  DigestAuth a = make_auth();
  char ch[200], h[400];
  a.challenge(10, false, ch, sizeof ch);
  for (int i = 0; i < 100; i++) {
    CHECK(a.check("Digest garbage", "GET", "/", 11, A) == AuthResult::Malformed);
    CHECK(a.check("Basic dXNlcjpwYXNz", "GET", "/", 11, A) == AuthResult::Malformed);
    // right password but another realm
    client_header(ch, "clearbell", "geheim-123", "GET", "/", 1, h, sizeof h);
    char *p = strstr(h, "realm=\"ClearBell test\"");
    CHECK(p != nullptr);
    if (p) p[16] = '_';  // "ClearBell test" -> "ClearBell_test"
    CHECK(a.check(h, "GET", "/", 11, A) == AuthResult::Malformed);
  }
  CHECK_EQ(a.fail_count(), 0u);
  CHECK(!a.locked(11, A));
  client_header(ch, "clearbell", "geheim-123", "GET", "/", 1, h, sizeof h);
  CHECK(a.check(h, "GET", "/", 11, A) == AuthResult::Ok);
}

TEST(auth_lock_time_grows_on_repetition_and_success_resets) {
  DigestAuth a = make_auth();
  char ch[200], h[400];
  unsigned nc = 1;
  uint32_t t = 100;
  const uint32_t expect[] = {30, 60, 120, 240, 480, 900, 900};  // doubles, capped at 15 min
  for (uint32_t e : expect) {
    CHECK(wrong_tries(a, A, DigestAuth::kMaxFailures, t, nc) == AuthResult::Bad);
    CHECK_EQ(a.retry_after_s(t, A), e);
    CHECK(a.locked(t + e - 1, A));
    CHECK(!a.locked(t + e, A));
    t += e;  // wait the lock out
  }
  // a success of that source resets: the next lock is 30 s again
  a.challenge(t, false, ch, sizeof ch);
  client_header(ch, "clearbell", "geheim-123", "GET", "/", nc++, h, sizeof h);
  CHECK(a.check(h, "GET", "/", t, A) == AuthResult::Ok);
  wrong_tries(a, A, DigestAuth::kMaxFailures, t + 1, nc);
  CHECK_EQ(a.retry_after_s(t + 1, A), DigestAuth::kLockS);
}

TEST(auth_table_overflow_keeps_running_lock) {
  DigestAuth a = make_auth();
  unsigned nc = 1;
  CHECK(wrong_tries(a, A, DigestAuth::kMaxFailures, 100, nc) == AuthResult::Bad);
  CHECK(a.locked(101, A));
  // more sources than table slots, each with one wrong password: A's lock must survive
  for (unsigned i = 0; i < 3 * DigestAuth::kSources; i++) wrong_tries(a, src(100 + i), 1, 101, nc);
  CHECK(a.locked(101, A));
  CHECK(!a.locked(101, src(100)));
}

TEST(auth_table_overflow_all_locked_replaces_oldest) {
  DigestAuth a = make_auth();
  unsigned nc = 1;
  for (int i = 0; i < DigestAuth::kSources; i++) wrong_tries(a, src(1 + i), DigestAuth::kMaxFailures, 100 + i, nc);
  for (int i = 0; i < DigestAuth::kSources; i++) CHECK(a.locked(110, src(1 + i)));
  // a ninth source kicks out the oldest locked one (source 1), the others stay locked
  wrong_tries(a, src(50), 1, 111, nc);
  CHECK(!a.locked(111, src(1)));
  CHECK(a.locked(111, src(2)));
  CHECK(a.locked(111, src(8)));
}

// Many forged sources: after 50 wrong passwords in 10 min at most one password check per second for everybody.
TEST(auth_global_brake_throttles_all_sources) {
  DigestAuth a = make_auth();
  char ch[200], h[400];
  unsigned nc = 1;
  uint32_t t = 1000;
  for (int i = 0; i < DigestAuth::kGlobalMaxFails; i++) wrong_tries(a, src(200 + i), 1, t, nc);
  CHECK(a.braking(t));
  a.challenge(t + 1, false, ch, sizeof ch);
  client_header(ch, "clearbell", "geheim-123", "GET", "/", nc++, h, sizeof h);
  CHECK(a.check(h, "GET", "/", t + 1, B) == AuthResult::Ok);  // throttled, not locked: first check this second
  client_header(ch, "clearbell", "geheim-123", "GET", "/", nc++, h, sizeof h);
  CHECK(a.check(h, "GET", "/", t + 1, A) == AuthResult::LockedOut);  // second in the same second
  CHECK_EQ(a.retry_after_s(t + 1, A), 1u);
  client_header(ch, "clearbell", "geheim-123", "GET", "/", nc++, h, sizeof h);
  CHECK(a.check(h, "GET", "/", t + 2, A) == AuthResult::Ok);  // next second: one check is possible again
  // the window ends: the brake is off
  CHECK(!a.braking(t + DigestAuth::kGlobalWindowS + 5));
}

TEST(auth_device_code_typed_like_the_label) {  // FW-B-14
  DigestAuth a;
  a.begin(kSecret, "ClearBell test", "clearbell", "K7QFM2XA9TRDH4WE");
  char ch[200], h[400];
  a.challenge(10, false, ch, sizeof ch);
  unsigned nc = 1;
  const char *forms[] = {"K7QFM2XA9TRDH4WE", "K7QF-M2XA-9TRD-H4WE", "K7QF M2XA 9TRD H4WE", "k7qfm2xa9trdh4we"};
  for (const char *typed : forms) {
    client_header(ch, "clearbell", typed, "GET", "/", nc++, h, sizeof h);
    CHECK(a.check(h, "GET", "/", 11, A) == AuthResult::Ok);
  }
  client_header(ch, "clearbell", "K7QF-M2XA-9TRD-H4WX", "GET", "/", nc++, h, sizeof h);
  CHECK(a.check(h, "GET", "/", 11, A) == AuthResult::Bad);
  // an own password gets no variants: "geheim-123" typed as "geheim123" stays wrong
  DigestAuth b = make_auth();
  b.challenge(10, false, ch, sizeof ch);
  client_header(ch, "clearbell", "geheim123", "GET", "/", 1, h, sizeof h);
  CHECK(b.check(h, "GET", "/", 11, A) == AuthResult::Bad);
}

TEST(auth_stale_nonces) {
  DigestAuth a = make_auth();
  char ch[200], h[400];
  a.challenge(1000, false, ch, sizeof ch);
  client_header(ch, "clearbell", "geheim-123", "GET", "/", 1, h, sizeof h);
  CHECK(a.check(h, "GET", "/", 1000 + DigestAuth::kNonceLifetimeS, A) == AuthResult::Ok);
  client_header(ch, "clearbell", "geheim-123", "GET", "/", 2, h, sizeof h);
  CHECK(a.check(h, "GET", "/", 1001 + DigestAuth::kNonceLifetimeS, A) == AuthResult::Stale);
  // a nonce from "before the reboot" (other secret) is stale, not a failure
  DigestAuth other;
  uint8_t s2[32] = {9};
  other.begin(s2, "ClearBell test", "clearbell", "geheim-123");
  other.challenge(1000, false, ch, sizeof ch);
  client_header(ch, "clearbell", "geheim-123", "GET", "/", 1, h, sizeof h);
  CHECK(a.check(h, "GET", "/", 1001, A) == AuthResult::Stale);
  CHECK_EQ(a.fail_count(), 0u);
  a.challenge(1000, true, ch, sizeof ch);
  CHECK(strstr(ch, "stale=true") != nullptr);
}

TEST(auth_forgotten_nonce_is_never_trusted_again) {
  DigestAuth a = make_auth();
  char first[200], ch[200], h[400];
  a.challenge(0, false, first, sizeof first);
  client_header(first, "clearbell", "geheim-123", "GET", "/", 1, h, sizeof h);
  CHECK(a.check(h, "GET", "/", 1, A) == AuthResult::Ok);
  char recorded[400];
  client_header(first, "clearbell", "geheim-123", "GET", "/", 2, recorded, sizeof recorded);
  // kTracked newer nonces push the first one out of the table
  for (int i = 0; i < DigestAuth::kTracked; i++) {
    a.challenge(2, false, ch, sizeof ch);
    client_header(ch, "clearbell", "geheim-123", "GET", "/", 1, h, sizeof h);
    CHECK(a.check(h, "GET", "/", 3, A) == AuthResult::Ok);
  }
  // its count is forgotten: a recorded header for it must not count as "first use" again
  CHECK(a.check(recorded, "GET", "/", 4, A) == AuthResult::Stale);
}

// ---- Door log ------------------------------------------------------------------------------------------------

TEST(door_log_ring_buffer_newest_first) {
  DoorLog l;
  CHECK_EQ(l.size(), 0);
  for (int i = 0; i < DoorLog::kCapacity + 5; i++) l.add(DoorEvent::Opened, (uint32_t)i, true, 7);
  CHECK_EQ(l.size(), DoorLog::kCapacity);
  CHECK_EQ(l.newest(0).time, (uint32_t)(DoorLog::kCapacity + 4));
  CHECK_EQ(l.newest(DoorLog::kCapacity - 1).time, 5u);
  CHECK_EQ(l.total_added(), (uint32_t)(DoorLog::kCapacity + 5));
  CHECK_EQ(l.newest(DoorLog::kCapacity).time, 0u);  // out of range -> empty entry, no crash
}

TEST(door_log_survives_serialisation) {
  DoorLog l;
  l.add(DoorEvent::Opened, 1762430000, true, 3);
  l.add(DoorEvent::NoAnswer, 125, false, 4);
  l.count_local();
  l.count_local();
  uint8_t blob[DoorLog::kBlobSize];
  l.serialize(blob);
  DoorLog m;
  CHECK(m.load(blob, sizeof blob));
  CHECK_EQ(m.size(), 2);
  CHECK_EQ(m.local_count(), 2u);
  CHECK(m.newest(0).event == DoorEvent::NoAnswer);
  CHECK(!m.newest(0).wall);
  CHECK_EQ(m.newest(0).boot, 4);
  CHECK(m.newest(1).wall);
  CHECK_EQ(m.newest(1).time, 1762430000u);
}

TEST(door_log_rejects_damaged_blobs) {
  DoorLog l;
  l.add(DoorEvent::Opened, 1, true, 1);
  uint8_t blob[DoorLog::kBlobSize];
  l.serialize(blob);
  DoorLog m;
  CHECK(!m.load(blob, sizeof blob - 1));  // wrong size
  uint8_t bad[DoorLog::kBlobSize];
  memcpy(bad, blob, sizeof bad);
  bad[2] = 99;  // other schema version
  CHECK(!m.load(bad, sizeof bad));
  memcpy(bad, blob, sizeof bad);
  bad[16 + 7] = 200;  // unknown event
  CHECK(!m.load(bad, sizeof bad));
  memcpy(bad, blob, sizeof bad);
  bad[3] = 5;  // count does not match head
  CHECK(!m.load(bad, sizeof bad));
  CHECK_EQ(m.size(), 0);  // a failed load leaves an empty log, never half of one
  // fuzz: random single-byte damage never crashes and never yields more than kCapacity entries
  for (int i = 0; i < (int)sizeof blob * 8; i++) {
    memcpy(bad, blob, sizeof bad);
    bad[i / 8] ^= (uint8_t)(1u << (i % 8));
    m.load(bad, sizeof bad);
    CHECK(m.size() <= DoorLog::kCapacity);
  }
}

// ---- JSON ------------------------------------------------------------------------------------------------------

TEST(json_writer_shape_and_escaping) {
  char buf[200];
  Json j(buf, sizeof buf);
  j.begin_obj();
  j.kv("a", 1);
  j.kv("s", "x\"y\\z\n");
  j.key("arr");
  j.begin_arr();
  j.num(-2);
  j.boolean(true);
  j.begin_obj();
  j.kv("k", 3u);
  j.end_obj();
  j.end_arr();
  j.key("r");
  j.real(1.25, 1);
  j.end_obj();
  CHECK(j.ok());
  CHECK(strcmp(buf, "{\"a\":1,\"s\":\"x\\\"y\\\\z\\u000a\",\"arr\":[-2,true,{\"k\":3}],\"r\":1.2}") == 0 ||
        strcmp(buf, "{\"a\":1,\"s\":\"x\\\"y\\\\z\\u000a\",\"arr\":[-2,true,{\"k\":3}],\"r\":1.3}") == 0);
}

TEST(json_writer_too_small_buffer_is_reported) {
  char buf[16];
  Json j(buf, sizeof buf);
  j.begin_obj();
  j.kv("long-key", "a long value that does not fit");
  j.end_obj();
  CHECK(!j.ok());
  CHECK(strlen(buf) < sizeof buf);
}

// Own password over plain HTTP (cb_http_auth.h): the page seals, the unit opens.
TEST(password_change_roundtrip_wrong_current_and_label) {
  const char *nonce = "0123456789abcdef0123456789abcdef";
  char enc[2 * kPasswordMax + 1], mac[33], out[kPasswordMax + 1];
  bool wrong = true;
  password_change_seal("Etikett-Code-123", nonce, "Innen-Eins-1", enc, mac);
  CHECK(strstr(enc, "496e6e65") == nullptr);  // "Inne..." does not appear in clear
  CHECK(password_change_open("Etikett-Code-123", nonce, enc, mac, out, sizeof out, &wrong));
  CHECK(!wrong);
  CHECK(strcmp(out, "Innen-Eins-1") == 0);
  // the 32-character maximum
  password_change_seal("cur", nonce, "abcdefghijklmnopqrstuvwxyz012345", enc, mac);
  CHECK(password_change_open("cur", nonce, enc, mac, out, sizeof out, &wrong));
  CHECK(strcmp(out, "abcdefghijklmnopqrstuvwxyz012345") == 0);
  // wrong current password: refused and reported as such
  password_change_seal("falsch", nonce, "geheim123", enc, mac);
  CHECK(!password_change_open("Etikett-Code-123", nonce, enc, mac, out, sizeof out, &wrong));
  CHECK(wrong);
  CHECK(out[0] == 0);
  // another nonce (replay of a recorded change): refused
  password_change_seal("Etikett-Code-123", nonce, "geheim123", enc, mac);
  CHECK(!password_change_open("Etikett-Code-123", "ffffffffffffffffffffffffffffffff", enc, mac, out, sizeof out, &wrong));
  // empty = back to the label
  password_change_seal("Innen-Eins-1", nonce, "", enc, mac);
  CHECK(password_change_open("Innen-Eins-1", nonce, enc, mac, out, sizeof out, &wrong));
  CHECK(out[0] == 0);
  // not allowed: too short, umlaut, leading space - even with the right current password
  const char *bads[] = {"abc", "Gr\xc3\xbc\xc3\x9f" "e12345", " abcdefghij", "abcdefghi"};
  for (const char *bad : bads) {
    password_change_seal("cur", nonce, bad, enc, mac);
    CHECK(!password_change_open("cur", nonce, enc, mac, out, sizeof out, &wrong));
    CHECK(!wrong);
  }
  CHECK(!password_allowed("AU-1"));
  CHECK(!password_allowed("abcdefghi"));  // RV-14: 9 characters are refused ...
  CHECK(password_allowed("abcdefghij"));  // ... 10 are accepted
  CHECK(!password_allowed("abcdefghijklmnopqrstuvwxyz0123456"));  // 33
}

// RV-14: a password of 9 characters is refused when set, 10 accepted - through the whole open path.
TEST(password_change_min_length_9_refused_10_accepted) {
  const char *nonce = "0123456789abcdef0123456789abcdef";
  char enc[2 * kPasswordMax + 1], mac[33], out[kPasswordMax + 1];
  bool wrong = true;
  password_change_seal("cur", nonce, "abcdefghi", enc, mac);
  CHECK(!password_change_open("cur", nonce, enc, mac, out, sizeof out, &wrong));
  CHECK(!wrong);
  password_change_seal("cur", nonce, "abcdefghij", enc, mac);
  CHECK(password_change_open("cur", nonce, enc, mac, out, sizeof out, &wrong));
  CHECK(strcmp(out, "abcdefghij") == 0);
  // a SHORT password that is already stored keeps working at boot (existing units stay reachable)
  DigestAuth a;
  a.begin(kSecret, "ClearBell test", "clearbell", "AU-1");
  char ch[200], h[400];
  a.challenge(10, false, ch, sizeof ch);
  client_header(ch, "clearbell", "AU-1", "GET", "/", 1, h, sizeof h);
  CHECK(a.check(h, "GET", "/", 11, A) == AuthResult::Ok);
}

// RV-43: the login takes the label code in several spellings; the password change must as well.
TEST(password_change_accepts_device_code_in_label_spellings) {
  const char *nonce = "0123456789abcdef0123456789abcdef";
  const char *code = "K7QFM2XA9TRDH4WE";
  char enc[2 * kPasswordMax + 1], mac[33], out[kPasswordMax + 1];
  bool wrong = true;
  const char *forms[] = {"K7QFM2XA9TRDH4WE", "K7QF-M2XA-9TRD-H4WE", "K7QF M2XA 9TRD H4WE", "k7qfm2xa9trdh4we"};
  for (const char *typed : forms) {
    password_change_seal(typed, nonce, "neues-Passwort-1", enc, mac);
    wrong = true;
    CHECK(password_change_open_stored(code, nonce, enc, mac, out, sizeof out, &wrong));
    CHECK(!wrong);
    CHECK(strcmp(out, "neues-Passwort-1") == 0);
  }
  // one wrong character: wrong current password
  password_change_seal("K7QF-M2XA-9TRD-H4WX", nonce, "neues-Passwort-1", enc, mac);
  CHECK(!password_change_open_stored(code, nonce, enc, mac, out, sizeof out, &wrong));
  CHECK(wrong);
  CHECK(out[0] == 0);
  // right code but a password that is not allowed: not "wrong current"
  password_change_seal("K7QF-M2XA-9TRD-H4WE", nonce, "kurz", enc, mac);
  CHECK(!password_change_open_stored(code, nonce, enc, mac, out, sizeof out, &wrong));
  CHECK(!wrong);
  // an own password is stored: the code (in any spelling) is not accepted, the own password is
  password_change_seal("K7QF-M2XA-9TRD-H4WE", nonce, "neues-Passwort-1", enc, mac);
  CHECK(!password_change_open_stored("eigenes-Passwort-9", nonce, enc, mac, out, sizeof out, &wrong));
  CHECK(wrong);
  password_change_seal("eigenes-Passwort-9", nonce, "neues-Passwort-1", enc, mac);
  CHECK(password_change_open_stored("eigenes-Passwort-9", nonce, enc, mac, out, sizeof out, &wrong));
  // the helper itself
  char v[kCodeVariantsMax][kCodeVariantLen];
  CHECK_EQ(device_code_variants(code, v), 4);
  CHECK(strcmp(v[0], code) == 0);
  CHECK_EQ(device_code_variants("eigenes-Passwort-9", v), 0);
}

// The page's JavaScript MD5 is checked against this: md5("cur:n:k1") as the page computes it.
TEST(password_change_known_vector_for_the_page) {
  char enc[2 * kPasswordMax + 1], mac[33];
  password_change_seal("cur", "n", "IN-1", enc, mac);
  printf("  vector: enc %s mac %s\n", enc, mac);
  char h[33];
  md5_hex("cur:n:IN-1", h);
  CHECK(strcmp(h, mac) == 0);
}