// test_pairing.cpp - port step 10b: pairing by device code, key wrap (cb_pairing_logic.h).
#include <cstring>

#include "cb_pairing_logic.h"
#include "mini_test.h"

using namespace cb;

namespace {
const char *kCode = "04HMASW9NF6YY41J";
uint8_t kKey[32], kNonce[kPairNonceLen];
void init() {
  for (int i = 0; i < 32; i++) kKey[i] = (uint8_t)(i * 13 + 5);
  for (size_t i = 0; i < kPairNonceLen; i++) kNonce[i] = (uint8_t)(i + 100);
}
}  // namespace

TEST(pairing_round_trip) {
  init();
  PairOffer o;
  CHECK(pair_seal(kCode, "f41138", "f3ac94", kKey, kNonce, &o));
  CHECK(memcmp(o.enc, kKey, 32) != 0);  // the key never travels in clear
  uint8_t got[32];
  CHECK(pair_open(kCode, "f41138", "f3ac94", o, got));
  CHECK(memcmp(got, kKey, 32) == 0);
  uint8_t c1[32], c2[32];
  CHECK(pair_confirm(got, o.nonce, 2, c1));
  CHECK(pair_confirm(kKey, kNonce, 2, c2));
  CHECK(memcmp(c1, c2, 32) == 0);
  CHECK(pair_confirm(kKey, kNonce, 3, c2));
  CHECK(memcmp(c1, c2, 32) != 0);  // the flat is bound into the confirmation
}

TEST(pairing_refuses_wrong_code_unit_or_tampering) {
  init();
  PairOffer o;
  pair_seal(kCode, "f41138", "f3ac94", kKey, kNonce, &o);
  uint8_t got[32];
  CHECK(!pair_open("04HMASW9NF6YY41K", "f41138", "f3ac94", o, got));  // one character off
  CHECK(!pair_open(kCode, "f41138", "aaaaaa", o, got));               // meant for another indoor unit
  CHECK(!pair_open(kCode, "bbbbbb", "f3ac94", o, got));               // another outdoor unit claimed
  for (int i = 0; i < 32; i++) {  // any flipped bit in the key part is detected
    PairOffer t = o;
    t.enc[i] ^= 1;
    CHECK(!pair_open(kCode, "f41138", "f3ac94", t, got));
  }
  PairOffer t = o;
  t.nonce[0] ^= 1;
  CHECK(!pair_open(kCode, "f41138", "f3ac94", t, got));
}

TEST(pairing_fresh_nonce_gives_different_ciphertext) {
  init();
  PairOffer a, b;
  pair_seal(kCode, "f41138", "f3ac94", kKey, kNonce, &a);
  kNonce[0] ^= 0xFF;
  pair_seal(kCode, "f41138", "f3ac94", kKey, kNonce, &b);
  CHECK(memcmp(a.enc, b.enc, 32) != 0);
}

TEST(wrap32_round_trip_and_tamper) {
  init();
  uint8_t plain[32], enc[32], tag[16], back[32];
  for (int i = 0; i < 32; i++) plain[i] = (uint8_t)(255 - i);
  CHECK(wrap32(kKey, "phone", kNonce, plain, 32, enc, tag));
  CHECK(unwrap32(kKey, "phone", kNonce, enc, 32, tag, back));
  CHECK(memcmp(back, plain, 32) == 0);
  CHECK(!unwrap32(kKey, "other", kNonce, enc, 32, tag, back));  // label binds the purpose
  enc[5] ^= 1;
  CHECK(!unwrap32(kKey, "phone", kNonce, enc, 32, tag, back));
  CHECK(!wrap32(kKey, "phone", kNonce, plain, 33, enc, tag));
}

TEST(hex_helpers) {
  uint8_t b[3] = {0x00, 0xAB, 0xFF}, back[3];
  char h[7];
  to_hex_lower(b, 3, h);
  CHECK(strcmp(h, "00abff") == 0);
  CHECK(from_hex("00ABff", back, 3) && memcmp(b, back, 3) == 0);
  CHECK(!from_hex("00ab", back, 3));
  CHECK(!from_hex("00abfg", back, 3));
  CHECK(!from_hex(nullptr, back, 3));
}

TEST(remote_names_are_stable_and_key_bound) {
  init();
  char o1[26], i1[26], u1[37], o2[26], i2[26], u2[37];
  CHECK(remote_names(kKey, o1, i1, u1));
  CHECK(remote_names(kKey, o2, i2, u2));
  CHECK(strcmp(o1, o2) == 0 && strcmp(i1, i2) == 0 && strcmp(u1, u2) == 0);  // both units compute the same
  CHECK(strncmp(o1, "cb-", 3) == 0 && strlen(o1) == 25 && strcmp(o1, i1) != 0);
  CHECK(strlen(u1) == 36 && u1[8] == '-' && u1[14] == '4');  // UUID v4 layout
  kKey[0] ^= 1;  // another pairing -> other names
  CHECK(remote_names(kKey, o2, i2, u2));
  CHECK(strcmp(o1, o2) != 0 && strcmp(u1, u2) != 0);
  for (const char *c = o1 + 3; *c; c++) CHECK((*c >= '0' && *c <= '9') || (*c >= 'a' && *c <= 'z'));
}

TEST(crypt32_round_trip) {
  init();
  uint8_t p[32], e[32], b[32];
  for (int i = 0; i < 32; i++) p[i] = (uint8_t)i;
  CHECK(crypt32(kKey, "phone", kNonce, p, 32, e));
  CHECK(memcmp(p, e, 32) != 0);
  CHECK(crypt32(kKey, "phone", kNonce, e, 32, b));
  CHECK(memcmp(p, b, 32) == 0);
}
