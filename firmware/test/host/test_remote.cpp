// test_remote.cpp - port step 9: token, phone signature, ntfy JSON lines (cb_remote_logic.h).
#include <cstdio>
#include <cstring>
#include <initializer_list>

#include "cb_remote_logic.h"
#include "mini_test.h"

using namespace cb;

namespace {

const char *kKeyH1 = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
const char *kKeyH2 = "fedcba9876543210fedcba9876543210fedcba9876543210fedcba9876543210";

void answer(const char *key, const char *token, const char *phone, char *out, size_t n) {
  char sig[65];
  RemoteAuth::sign(key, token, phone, sig);
  snprintf(out, n, "CBR1 %s %s %s", token, phone, sig);
}

}  // namespace

TEST(remote_signature_matches_python_reference) {
  // python: hmac.new(key.encode(), b"CBR1|" + token.encode() + b"|H1", sha256).hexdigest()
  char sig[65];
  CHECK(RemoteAuth::sign(kKeyH1, "00112233445566778899aabbccddeeff", "H1", sig));
  CHECK(strcmp(sig, "35ed6d75123adbf5fb8989d3e3565a0520a79de0d4754a86b86b7135a08bb1f3") == 0);
}

TEST(remote_happy_path_opens_once) {
  RemoteAuth a;
  CHECK(a.set_phone(0, "H1", kKeyH1));
  char tok[33], msg[200], who[9];
  a.issue(1000, tok);
  CHECK_EQ(strlen(tok), 32u);
  answer(kKeyH1, tok, "H1", msg, sizeof msg);
  CHECK(a.check(msg, 2000, who) == RemoteResult::Ok);
  CHECK(strcmp(who, "H1") == 0);
  // check alone does not use the token (a lock-out refusal leaves it usable) ...
  CHECK(a.check(msg, 3000, who) == RemoteResult::Ok);
  // ... the opening does
  a.consume_all();
  CHECK(a.check(msg, 4000, who) == RemoteResult::UnknownToken);
  CHECK_EQ(a.active(4000), 0);
}

TEST(remote_refuses_wrong_phone_key_token_and_age) {
  RemoteAuth a;
  a.set_phone(0, "H1", kKeyH1);
  a.set_phone(1, "H2", kKeyH2);
  char tok[33], msg[200], who[9];
  a.issue(0, tok);
  // signed with H2's key but claiming H1
  answer(kKeyH2, tok, "H1", msg, sizeof msg);
  CHECK(a.check(msg, 10, who) == RemoteResult::BadSignature);
  CHECK(who[0] == 0);
  // H2 itself is fine
  answer(kKeyH2, tok, "H2", msg, sizeof msg);
  CHECK(a.check(msg, 10, who) == RemoteResult::Ok);
  // unknown phone
  answer(kKeyH1, tok, "H9", msg, sizeof msg);
  CHECK(a.check(msg, 10, who) == RemoteResult::UnknownPhone);
  // a token that was never issued
  answer(kKeyH1, "00112233445566778899aabbccddeeff", "H1", msg, sizeof msg);
  CHECK(a.check(msg, 10, who) == RemoteResult::UnknownToken);
  // too old
  answer(kKeyH1, tok, "H1", msg, sizeof msg);
  CHECK(a.check(msg, kRemoteTokenMs, who) == RemoteResult::Ok);
  CHECK(a.check(msg, kRemoteTokenMs + 1, who) == RemoteResult::Expired);
  // signature for another token does not carry over
  char tok2[33], sig_old[65], forged[200];
  a.issue(100, tok2);
  RemoteAuth::sign(kKeyH1, tok, "H1", sig_old);
  snprintf(forged, sizeof forged, "CBR1 %s H1 %s", tok2, sig_old);
  CHECK(a.check(forged, 200, who) == RemoteResult::BadSignature);
}

TEST(remote_token_ring_keeps_newest_three) {
  RemoteAuth a;
  a.set_phone(0, "H1", kKeyH1);
  char t[4][33], msg[200], who[9];
  for (int i = 0; i < 4; i++) a.issue((uint32_t)i, t[i]);
  answer(kKeyH1, t[0], "H1", msg, sizeof msg);
  CHECK(a.check(msg, 10, who) == RemoteResult::UnknownToken);  // pushed out by the 4th ring
  for (int i = 1; i < 4; i++) {
    answer(kKeyH1, t[i], "H1", msg, sizeof msg);
    CHECK(a.check(msg, 10, who) == RemoteResult::Ok);
  }
  CHECK(strcmp(t[1], t[2]) != 0);
}

TEST(remote_message_format) {
  RemoteAuth a;
  a.set_phone(0, "H1", kKeyH1);
  char tok[33], msg[200], who[9];
  a.issue(0, tok);
  answer(kKeyH1, tok, "H1", msg, sizeof msg);
  // upper-case signature and trailing newline (what phone tools produce) are accepted
  char up[200];
  snprintf(up, sizeof up, "%s\n", msg);
  for (char *c = up + 5 + 32 + 4; *c; c++)
    if (*c >= 'a' && *c <= 'f') *c = (char)(*c - 'a' + 'A');
  CHECK(a.check(up, 1, who) == RemoteResult::Ok);
  const char *bad[] = {"", "CBR1", "CBR1 x y z", "CBR2 00112233445566778899aabbccddeeff H1 00",
                       "CBR1  00112233445566778899aabbccddeeff H1 0", "hallo welt", nullptr};
  CHECK(a.check(bad[0], 1, who) == RemoteResult::Malformed);
  CHECK(a.check(bad[1], 1, who) == RemoteResult::Malformed);
  CHECK(a.check(bad[2], 1, who) == RemoteResult::Malformed);
  CHECK(a.check(bad[3], 1, who) == RemoteResult::UnknownScheme);
  CHECK(a.check(bad[4], 1, who) == RemoteResult::Malformed);
  CHECK(a.check(bad[5], 1, who) == RemoteResult::Malformed);
  CHECK(a.check(nullptr, 1, who) == RemoteResult::Malformed);
  // five words
  char five[220];
  snprintf(five, sizeof five, "%s extra", msg);
  CHECK(a.check(five, 1, who) == RemoteResult::Malformed);
}

TEST(remote_phone_provisioning_checks) {
  RemoteAuth a;
  CHECK(!a.set_phone(0, "", kKeyH1));
  CHECK(!a.set_phone(0, "viel-zu-lang", kKeyH1));
  CHECK(!a.set_phone(0, "H 1", kKeyH1));
  CHECK(!a.set_phone(0, "H1", "abc"));
  CHECK(!a.set_phone(0, "H1", "0123456789ABCDEF0123456789abcdef0123456789abcdef0123456789abcdef"));
  CHECK(!a.set_phone(kRemotePhones, "H1", kKeyH1));
  CHECK_EQ(a.phone_count(), 0);
  CHECK(a.set_phone(3, "Kind-1", kKeyH1));
  CHECK_EQ(a.phone_count(), 1);
}

TEST(json_get_string_reads_ntfy_lines) {
  const char *line =
      "{\"id\":\"hwQ2YpKdmg\",\"time\":1762440000,\"expires\":1762483200,\"event\":\"message\",\"topic\":\"cb-x\","
      "\"message\":\"CBR1 a\\\"b\\\\c d\",\"tags\":[\"x\",\"y\"],\"extra\":{\"message\":\"inner\"}}";
  char v[100];
  CHECK(json_get_string(line, "event", v, sizeof v));
  CHECK(strcmp(v, "message") == 0);
  CHECK(json_get_string(line, "message", v, sizeof v));
  CHECK(strcmp(v, "CBR1 a\"b\\c d") == 0);
  CHECK(json_get_string(line, "id", v, sizeof v));
  CHECK(strcmp(v, "hwQ2YpKdmg") == 0);
  CHECK(!json_get_string(line, "time", v, sizeof v));     // a number, not a string
  CHECK(!json_get_string(line, "missing", v, sizeof v));
  char small[4];
  CHECK(!json_get_string(line, "message", small, sizeof small));  // does not fit -> refused, not cut
  // a value that LOOKS like a key must not be taken for one
  const char *trick = "{\"title\":\"\\\"message\\\":\\\"fake\\\"\",\"message\":\"real\"}";
  CHECK(json_get_string(trick, "message", v, sizeof v));
  CHECK(strcmp(v, "real") == 0);
  // keepalive line
  CHECK(json_get_string("{\"id\":\"a\",\"time\":1,\"event\":\"keepalive\",\"topic\":\"t\"}", "event", v, sizeof v));
  CHECK(strcmp(v, "keepalive") == 0);
}

TEST(json_get_string_survives_garbage) {
  char v[64];
  const char *junk[] = {"", "{", "}", "{\"", "{\"a\"", "{\"a\":", "{\"a\":\"", "{\"a\":\"\\", "{\"a\":\"\\u12",
                        "[1,2]", "{\"a\":[[[[[[[[[[[1]]]]]]]]]],\"message\":\"x\"}", "{\"a\":tru", "nonsense",
                        "{\"a\":{\"b\":{\"c\":\"d\"}},\"message\":\"ok\"}"};
  for (const char *j : junk) json_get_string(j, "message", v, sizeof v);
  CHECK(json_get_string(junk[13], "message", v, sizeof v));
  CHECK(strcmp(v, "ok") == 0);
  CHECK(!json_get_string(junk[10], "message", v, sizeof v));  // nested too deep -> refused
  // byte-wise mutation of a real line never crashes
  char line[] = "{\"id\":\"a\",\"event\":\"message\",\"message\":\"CBR1 0011 H1 ff\"}";
  for (size_t i = 0; i < sizeof line - 1; i++) {
    for (int c : {0x22, 0x5c, 0x7b, 0x7d, 0x3a, 0x2c, 0x00 + 1, 0x41}) {
      char m[sizeof line];
      memcpy(m, line, sizeof line);
      m[i] = (char)c;
      json_get_string(m, "message", v, sizeof v);
    }
  }
  CHECK(true);
}

TEST(remote_phones_added_and_removed_at_runtime) {
  RemoteAuth a;
  CHECK_EQ(a.add_phone("H1", kKeyH1), 0);
  CHECK_EQ(a.add_phone("H2", kKeyH2), 1);
  CHECK_EQ(a.add_phone("H1", kKeyH2), 0);  // same id = replaced (new phone of the same person)
  char tok[33], msg[200], who[9];
  a.issue(0, tok);
  answer(kKeyH1, tok, "H1", msg, sizeof msg);
  CHECK(a.check(msg, 1, who) == RemoteResult::BadSignature);  // the old key of H1 is gone
  answer(kKeyH2, tok, "H1", msg, sizeof msg);
  CHECK(a.check(msg, 1, who) == RemoteResult::Ok);
  CHECK(a.remove_phone("H1"));
  CHECK(a.check(msg, 1, who) == RemoteResult::UnknownPhone);  // removed = locked out at once
  CHECK(!a.remove_phone("H1"));
  CHECK_EQ(a.add_phone("A", kKeyH1), 0);
  CHECK_EQ(a.add_phone("B", kKeyH1), 2);
  CHECK_EQ(a.add_phone("C", kKeyH1), 3);
  CHECK_EQ(a.add_phone("D", kKeyH1), -1);  // full
  CHECK_EQ(a.add_phone("bad id", kKeyH1), -1);
}
