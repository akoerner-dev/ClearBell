// test_remote.cpp - remote opening R27: topics per phone, tokens per phone and ring, flats, ntfy JSON lines
// (cb_remote_logic.h).
#include <cstdio>
#include <cstring>
#include <initializer_list>

#include "cb_remote_logic.h"
#include "mini_test.h"

using namespace cb;

namespace {

const char *kKeyH1 = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
const char *kKeyH2 = "fedcba9876543210fedcba9876543210fedcba9876543210fedcba9876543210";
const char *kKeyH3 = "00112233445566778899aabbccddeeff00112233445566778899aabbccddeeff";
constexpr uint8_t EG = 2, OG = 3;

void cmd(const char *token, char *out, size_t n) { snprintf(out, n, "CBR2 %s", token); }

}  // namespace

TEST(remote_topics_match_python_reference) {
  // python: "cb" + hmac.new(key.encode(), b"CBT1|push", sha256).hexdigest()[:32]  (and b"CBT1|cmd")
  char push[kTopicLen + 1], c[kTopicLen + 1];
  CHECK(phone_topics(kKeyH1, push, c));
  CHECK(strcmp(push, "cbc2b2393790f445ccfdb58265f973d4b4") == 0);
  CHECK(strcmp(c, "cb70c03e2af649e2b1634e2b4e7d41fd3a") == 0);
  CHECK(!phone_topics("abc", push, c));
  CHECK(!phone_topics("0123456789ABCDEF0123456789abcdef0123456789abcdef0123456789abcdef", push, c));
}

TEST(remote_happy_path_opens_once) {
  RemoteAuth a;
  CHECK(a.set_phone(0, "H1", kKeyH1, EG));
  int slots[kPhonesPerFlat];
  char tok[kPhonesPerFlat][33], msg[64];
  CHECK_EQ(a.issue(EG, 1000, slots, tok), 1);
  CHECK_EQ(slots[0], 0);
  CHECK_EQ(strlen(tok[0]), 32u);
  cmd(tok[0], msg, sizeof msg);
  int who = -1;
  CHECK(a.check(a.cmd_topic(0), msg, 2000, &who) == RemoteResult::Ok);
  CHECK_EQ(who, 0);
  // check alone does not use the token (a lock-out refusal leaves it usable) ...
  CHECK(a.check(a.cmd_topic(0), msg, 3000, &who) == RemoteResult::Ok);
  // ... the opening does
  a.consume_flat(EG);
  CHECK(a.check(a.cmd_topic(0), msg, 4000, &who) == RemoteResult::UnknownToken);
  CHECK_EQ(a.active(4000), 0);
}

TEST(remote_token_belongs_to_one_phone_and_flat) {
  RemoteAuth a;
  a.set_phone(0, "H1", kKeyH1, EG);
  a.set_phone(1, "H2", kKeyH2, EG);
  a.set_phone(2, "H3", kKeyH3, OG);
  int slots[kPhonesPerFlat], who;
  char tok[kPhonesPerFlat][33], msg[64];
  CHECK_EQ(a.issue(EG, 0, slots, tok), 2);  // both EG phones, not the OG phone
  cmd(tok[0], msg, sizeof msg);
  // H1's token sent on H2's command topic: refused (a phone cannot use another phone's token)
  CHECK(a.check(a.cmd_topic(slots[1]), msg, 10, &who) == RemoteResult::UnknownToken);
  CHECK(a.check(a.cmd_topic(2), msg, 10, &who) == RemoteResult::UnknownToken);  // OG phone
  CHECK(a.check(a.cmd_topic(slots[0]), msg, 10, &who) == RemoteResult::Ok);
  // a topic that belongs to nobody
  CHECK(a.check("cb00000000000000000000000000000000", msg, 10, &who) == RemoteResult::UnknownPhone);
  CHECK(a.check(nullptr, msg, 10, &who) == RemoteResult::UnknownPhone);
  // a token that was never issued
  CHECK(a.check(a.cmd_topic(0), "CBR2 00112233445566778899aabbccddeeff", 10, &who) == RemoteResult::UnknownToken);
  // too old
  CHECK(a.check(a.cmd_topic(slots[0]), msg, kRemoteTokenMs, &who) == RemoteResult::Ok);
  CHECK(a.check(a.cmd_topic(slots[0]), msg, kRemoteTokenMs + 1, &who) == RemoteResult::Expired);
  // opening at EG uses up the EG tokens only
  char og[kPhonesPerFlat][33], ogmsg[64];
  int ogslots[kPhonesPerFlat];
  CHECK_EQ(a.issue(OG, 20, ogslots, og), 1);
  a.consume_flat(EG);
  CHECK(a.check(a.cmd_topic(slots[0]), msg, 30, &who) == RemoteResult::UnknownToken);
  cmd(og[0], ogmsg, sizeof ogmsg);
  CHECK(a.check(a.cmd_topic(2), ogmsg, 30, &who) == RemoteResult::Ok);
  CHECK_EQ(who, 2);
}

TEST(remote_keeps_newest_three_rings_per_flat) {
  RemoteAuth a;
  a.set_phone(0, "H1", kKeyH1, EG);
  a.set_phone(1, "H3", kKeyH3, OG);
  int s[kPhonesPerFlat], who;
  char t[4][kPhonesPerFlat][33], og[kPhonesPerFlat][33], msg[64];
  a.issue(OG, 0, s, og);
  for (int i = 0; i < 4; i++) a.issue(EG, (uint32_t)i, s, t[i]);
  cmd(t[0][0], msg, sizeof msg);
  CHECK(a.check(a.cmd_topic(0), msg, 10, &who) == RemoteResult::UnknownToken);  // pushed out by the 4th ring
  for (int i = 1; i < 4; i++) {
    cmd(t[i][0], msg, sizeof msg);
    CHECK(a.check(a.cmd_topic(0), msg, 10, &who) == RemoteResult::Ok);
  }
  cmd(og[0], msg, sizeof msg);  // the OG ring is untouched by four EG rings
  CHECK(a.check(a.cmd_topic(1), msg, 10, &who) == RemoteResult::Ok);
  CHECK(strcmp(t[1][0], t[2][0]) != 0);
}

TEST(remote_message_format) {
  RemoteAuth a;
  a.set_phone(0, "H1", kKeyH1, EG);
  int s[kPhonesPerFlat], who;
  char tok[kPhonesPerFlat][33], msg[64];
  a.issue(EG, 0, s, tok);
  snprintf(msg, sizeof msg, "  CBR2 %s\n", tok[0]);  // surrounding whitespace is accepted
  CHECK(a.check(a.cmd_topic(0), msg, 1, &who) == RemoteResult::Ok);
  const char *topic = a.cmd_topic(0);
  CHECK(a.check(topic, "", 1, &who) == RemoteResult::Malformed);
  CHECK(a.check(topic, "CBR2", 1, &who) == RemoteResult::Malformed);
  CHECK(a.check(topic, "CBR2 xyz", 1, &who) == RemoteResult::Malformed);
  CHECK(a.check(topic, "CBR1 00112233445566778899aabbccddeeff", 1, &who) == RemoteResult::UnknownScheme);
  CHECK(a.check(topic, "CBR2 00112233445566778899AABBCCDDEEFF", 1, &who) == RemoteResult::Malformed);
  CHECK(a.check(topic, "hallo welt", 1, &who) == RemoteResult::Malformed);
  CHECK(a.check(topic, nullptr, 1, &who) == RemoteResult::Malformed);
  snprintf(msg, sizeof msg, "CBR2 %s extra", tok[0]);
  CHECK(a.check(topic, msg, 1, &who) == RemoteResult::Malformed);
  CHECK_EQ(who, -1);
}

TEST(remote_phone_provisioning_checks) {
  RemoteAuth a;
  CHECK(!a.set_phone(0, "", kKeyH1, EG));
  CHECK(!a.set_phone(0, "viel-zu-lang", kKeyH1, EG));
  CHECK(!a.set_phone(0, "H 1", kKeyH1, EG));
  CHECK(!a.set_phone(0, "H1", "abc", EG));
  CHECK(!a.set_phone(0, "H1", kKeyH1, 1));  // outdoor is no flat
  CHECK(!a.set_phone(kRemotePhones, "H1", kKeyH1, EG));
  CHECK_EQ(a.phone_count(), 0);
  CHECK(a.set_phone(3, "Kind-1", kKeyH1, OG));
  CHECK_EQ(a.phone_count(), 1);
  CHECK_EQ(a.phone_count(OG), 1);
  CHECK_EQ(a.phone_count(EG), 0);
  char list[200];
  CHECK(a.cmd_topics(list, sizeof list));
  CHECK(strcmp(list, a.cmd_topic(3)) == 0);
  a.set_phone(0, "H2", kKeyH2, EG);
  CHECK(a.cmd_topics(list, sizeof list));
  CHECK_EQ(strlen(list), 2 * kTopicLen + 1);
  char tiny[10];
  CHECK(!a.cmd_topics(tiny, sizeof tiny));
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
  CHECK_EQ(a.add_phone("H1", kKeyH1, EG), 0);
  CHECK_EQ(a.add_phone("H2", kKeyH2, EG), 1);
  CHECK_EQ(a.add_phone("H1", kKeyH3, OG), 2);  // same name in the other flat = another phone
  int s[kPhonesPerFlat], who;
  char tok[kPhonesPerFlat][33], msg[64];
  a.issue(EG, 0, s, tok);
  cmd(tok[0], msg, sizeof msg);
  char old_topic[kTopicLen + 1];
  snprintf(old_topic, sizeof old_topic, "%s", a.cmd_topic(0));
  const char *kKeyH4 = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
  CHECK_EQ(a.add_phone("H1", kKeyH4, EG), 0);  // same id + flat = replaced (new phone of the same person) ...
  CHECK(a.check(old_topic, msg, 1, &who) == RemoteResult::UnknownPhone);  // ... the old topics are gone
  CHECK(a.check(a.cmd_topic(0), msg, 1, &who) == RemoteResult::UnknownToken);  // and it inherits no token
  cmd(tok[1], msg, sizeof msg);
  CHECK(a.check(a.cmd_topic(1), msg, 1, &who) == RemoteResult::Ok);
  CHECK(a.remove_phone("H2", EG));
  CHECK(a.check(a.cmd_topic(1), msg, 1, &who) == RemoteResult::UnknownPhone);  // removed = locked out at once
  CHECK(!a.remove_phone("H2", EG));
  CHECK(!a.remove_phone("H1", 1));
  CHECK_EQ(a.add_phone("A", kKeyH1, EG), 1);
  CHECK_EQ(a.add_phone("B", kKeyH1, EG), 3);
  CHECK_EQ(a.add_phone("C", kKeyH1, EG), 4);
  CHECK_EQ(a.add_phone("D", kKeyH1, EG), -1);  // EG full (4) ...
  CHECK_EQ(a.add_phone("D", kKeyH1, OG), 5);   // ... OG still has room
  CHECK_EQ(a.add_phone("bad id", kKeyH1, OG), -1);
}
