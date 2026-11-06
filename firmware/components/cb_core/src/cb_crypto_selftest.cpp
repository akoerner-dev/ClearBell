// cb_crypto_selftest.cpp - RFC 4231 known-answer tests for hmac_sha256().
//
// Platform-neutral: runs in the PC unit tests and, via the console command "selftest", on the device.
// Case 5 of the RFC (128-bit truncation of a different input) is left out because it adds nothing
// beyond case 1-4 for a full-length implementation; the truncation itself is tested in test_protocol.
#include <cstring>

#include "cb_crypto.h"

namespace cb {

namespace {

struct Vector {
  const uint8_t *key;
  size_t key_len;
  const uint8_t *data;
  size_t data_len;
  const char *expected_hex;  // 64 hex digits
};

int hex_nibble(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  return -1;
}

bool matches_hex(const uint8_t *mac, const char *hex) {
  for (size_t i = 0; i < kMacLen; i++) {
    int hi = hex_nibble(hex[2 * i]), lo = hex_nibble(hex[2 * i + 1]);
    if (hi < 0 || lo < 0 || mac[i] != (uint8_t)(hi << 4 | lo)) return false;
  }
  return true;
}

}  // namespace

int hmac_selftest() {
  static uint8_t k0b[20], kaa20[20], k01[25], kaa131[131], ddd[50], dcd[50];
  memset(k0b, 0x0b, sizeof(k0b));
  memset(kaa20, 0xaa, sizeof(kaa20));
  for (size_t i = 0; i < sizeof(k01); i++) k01[i] = (uint8_t)(i + 1);
  memset(kaa131, 0xaa, sizeof(kaa131));
  memset(ddd, 0xdd, sizeof(ddd));
  memset(dcd, 0xcd, sizeof(dcd));

  static const char d1[] = "Hi There";
  static const char k2[] = "Jefe";
  static const char d2[] = "what do ya want for nothing?";
  static const char d6[] = "Test Using Larger Than Block-Size Key - Hash Key First";
  static const char d7[] =
      "This is a test using a larger than block-size key and a larger than block-size data. The key needs "
      "to be hashed before being used by the HMAC algorithm.";

  const Vector v[] = {
      {k0b, 20, (const uint8_t *)d1, sizeof(d1) - 1,
       "b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7"},
      {(const uint8_t *)k2, 4, (const uint8_t *)d2, sizeof(d2) - 1,
       "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843"},
      {kaa20, 20, ddd, 50, "773ea91e36800e46854db8ebd09181a72959098b3ef8c122d9635514ced565fe"},
      {k01, 25, dcd, 50, "82558a389a443c0ea4cc819899f2083a85f0faa3e578f8077a2e3ff46729665b"},
      {kaa131, 131, (const uint8_t *)d6, sizeof(d6) - 1,
       "60e431591ee0b67f0d8a26aacbf5b77f8e0bc6213728c5140546040f0ee37f54"},
      {kaa131, 131, (const uint8_t *)d7, sizeof(d7) - 1,
       "9b09ffa71b942fcb27635fbcd5b0e944bfdc63644f0713938a7f51535c3a35e2"},
  };

  int failed = 0;
  for (const Vector &t : v) {
    uint8_t mac[kMacLen];
    // Split the message into two slices to also exercise the multi-part path that frames use.
    size_t half = t.data_len / 2;
    Slice parts[2] = {{t.data, half}, {t.data + half, t.data_len - half}};
    if (!hmac_sha256(t.key, t.key_len, parts, 2, mac) || !matches_hex(mac, t.expected_hex)) failed++;
  }
  return failed;
}

}  // namespace cb
