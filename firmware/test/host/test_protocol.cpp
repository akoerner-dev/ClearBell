// test_protocol.cpp - crypto known answers and protocol v2 framing (Softwarearchitektur 11.2, "Komponente"
// and "Sicherheit": wrong tag, foreign receiver, unknown version are rejected).
#include <cstring>

#include "cb_bytes.h"
#include "cb_crypto.h"
#include "cb_protocol.h"
#include "mini_test.h"

using namespace cb;

namespace {

uint8_t key_a[kKeyLen], key_b[kKeyLen];

void make_keys() {
  for (size_t i = 0; i < kKeyLen; i++) {
    key_a[i] = (uint8_t)i;
    key_b[i] = (uint8_t)(0xA0 + i);
  }
}

Frame ring_frame() {
  Frame f{};
  f.type = MsgType::Ring;
  f.sender = UnitId::Outdoor;
  f.receiver = UnitId::EG;
  f.session = 0x11223344;
  f.seq = 5;
  return f;
}

}  // namespace

TEST(hmac_rfc4231) {
  CHECK_EQ(hmac_selftest(), 0);
}

TEST(little_endian_helpers) {
  uint8_t b[4];
  put_u32le(b, 0x11223344);
  CHECK(b[0] == 0x44 && b[1] == 0x33 && b[2] == 0x22 && b[3] == 0x11);
  CHECK_EQ(get_u32le(b), 0x11223344);
  put_u16le(b, 0xBEEF);
  CHECK_EQ(get_u16le(b), 0xBEEF);
}

TEST(ct_equal_basic) {
  uint8_t a[16] = {1, 2, 3}, b[16] = {1, 2, 3};
  CHECK(ct_equal(a, b, 16));
  b[15] = 1;
  CHECK(!ct_equal(a, b, 16));
}

// Pins the wire format: the expected bytes were computed independently with Python
// (hmac.new(bytes(range(32)), b"CBv2" + header, sha256).digest()[:16]). If this test fails after a code
// change, the change broke compatibility with units running older firmware.
TEST(golden_frame) {
  make_keys();
  uint8_t out[kMaxFrame];
  size_t n = encode_frame(ring_frame(), key_a, out, sizeof(out));
  static const uint8_t expected[] = {0x43, 0x42, 0x02, 0x03, 0x01, 0x02, 0x44, 0x33, 0x22, 0x11,
                                     0x05, 0x00, 0x00, 0x00, 0x6e, 0x2b, 0xe7, 0x13, 0x98, 0x61,
                                     0x1c, 0x02, 0xa7, 0x13, 0xac, 0xd1, 0xe6, 0x36, 0xc4, 0x09};
  CHECK_EQ(n, sizeof(expected));
  CHECK(memcmp(out, expected, sizeof(expected)) == 0);
}

TEST(roundtrip_with_payload) {
  make_keys();
  uint8_t payload[kNonceLen];
  for (size_t i = 0; i < sizeof(payload); i++) payload[i] = (uint8_t)(0x55 ^ i);
  Frame f{};
  f.type = MsgType::Ping;
  f.sender = UnitId::OG;
  f.receiver = UnitId::Outdoor;
  f.session = 0xDEADBEEF;
  f.seq = 0xFFFFFFFE;
  f.payload = payload;
  f.payload_len = sizeof(payload);
  uint8_t buf[kMaxFrame];
  size_t n = encode_frame(f, key_a, buf, sizeof(buf));
  CHECK_EQ(n, kHeaderLen + kNonceLen + kTagLen);
  Frame g{};
  CHECK(verify_frame(buf, n, key_a, &g) == RxError::Ok);
  CHECK(g.type == MsgType::Ping);
  CHECK(g.sender == UnitId::OG && g.receiver == UnitId::Outdoor);
  CHECK_EQ(g.session, 0xDEADBEEF);
  CHECK_EQ(g.seq, 0xFFFFFFFE);
  CHECK_EQ(g.payload_len, kNonceLen);
  CHECK(memcmp(g.payload, payload, kNonceLen) == 0);
}

TEST(payload_too_long_is_refused) {
  make_keys();
  uint8_t big[kMaxPayload + 1] = {0};
  Frame f = ring_frame();
  f.payload = big;
  f.payload_len = sizeof(big);
  uint8_t buf[kMaxFrame + 8];
  CHECK_EQ(encode_frame(f, key_a, buf, sizeof(buf)), 0);
  f.payload_len = kMaxPayload;  // exactly the maximum is fine
  CHECK_EQ(encode_frame(f, key_a, buf, sizeof(buf)), kMaxFrame);
  CHECK_EQ(encode_frame(f, key_a, buf, kMaxFrame - 1), 0);  // output buffer too small
}

TEST(reject_wrong_key_and_any_bit_flip) {
  make_keys();
  uint8_t buf[kMaxFrame];
  size_t n = encode_frame(ring_frame(), key_a, buf, sizeof(buf));
  Frame g;
  CHECK(verify_frame(buf, n, key_b, &g) == RxError::BadTag);
  // Flip every single bit of the authenticated area and the tag: nothing may get through as Ok.
  int accepted = 0, flips = 0;
  for (size_t i = 0; i < n; i++) {
    for (int bit = 0; bit < 8; bit++) {
      buf[i] ^= (uint8_t)(1 << bit);
      if (verify_frame(buf, n, key_a, &g) == RxError::Ok) accepted++;
      buf[i] ^= (uint8_t)(1 << bit);
      flips++;
    }
  }
  CHECK_EQ(flips, (int)n * 8);
  CHECK_EQ(accepted, 0);
  CHECK(verify_frame(buf, n, key_a, &g) == RxError::Ok);  // unmodified frame still fine
}

TEST(reject_by_precheck_reason) {
  make_keys();
  uint8_t buf[kMaxFrame];
  size_t n = encode_frame(ring_frame(), key_a, buf, sizeof(buf));
  Frame g;
  CHECK(verify_frame(buf, kMinFrame - 1, key_a, &g) == RxError::BadLength);
  CHECK(verify_frame(buf, kMaxFrame + 1, key_a, &g) == RxError::BadLength);
  uint8_t copy[kMaxFrame];
  memcpy(copy, buf, n);
  copy[0] = 'X';
  CHECK(verify_frame(copy, n, key_a, &g) == RxError::BadMagic);
  memcpy(copy, buf, n);
  copy[2] = 1;  // V0.1 or unknown version
  CHECK(verify_frame(copy, n, key_a, &g) == RxError::BadVersion);
  memcpy(copy, buf, n);
  copy[5] = 9;  // receiver that does not exist
  CHECK(verify_frame(copy, n, key_a, &g) == RxError::BadAddress);
  memcpy(copy, buf, n);
  copy[5] = copy[4];  // sender == receiver
  CHECK(verify_frame(copy, n, key_a, &g) == RxError::BadAddress);
}

TEST(precheck_returns_unauthenticated_addresses) {
  make_keys();
  uint8_t buf[kMaxFrame];
  size_t n = encode_frame(ring_frame(), key_a, buf, sizeof(buf));
  UnitId s = UnitId::None, r = UnitId::None;
  CHECK(precheck_frame(buf, n, &s, &r) == RxError::Ok);
  CHECK(s == UnitId::Outdoor && r == UnitId::EG);
}
