// crypto_host.cpp - PC implementation of cb_crypto.h for the unit tests.
//
// A compact reference SHA-256 (FIPS 180-4) and HMAC (RFC 2104). It is checked by the RFC 4231 vectors in
// cb_crypto_selftest.cpp, which were in turn cross-checked against Python's hmac module. Never linked into
// the firmware: there, cb_crypto_esp.cpp uses mbedtls and the hardware SHA unit.
//
// random_bytes() is a deterministic generator here, so test runs are reproducible.
#include <cstring>

#include "cb_crypto.h"

namespace {

struct Sha256 {
  uint32_t h[8];
  uint8_t block[64];
  size_t block_len;
  uint64_t total_len;
};

const uint32_t K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

uint32_t rotr(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }

void compress(Sha256 &s, const uint8_t *p) {
  uint32_t w[64];
  for (int i = 0; i < 16; i++)
    w[i] = (uint32_t)p[4 * i] << 24 | (uint32_t)p[4 * i + 1] << 16 | (uint32_t)p[4 * i + 2] << 8 | p[4 * i + 3];
  for (int i = 16; i < 64; i++) {
    uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
    uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
    w[i] = w[i - 16] + s0 + w[i - 7] + s1;
  }
  uint32_t a = s.h[0], b = s.h[1], c = s.h[2], d = s.h[3], e = s.h[4], f = s.h[5], g = s.h[6], h = s.h[7];
  for (int i = 0; i < 64; i++) {
    uint32_t t1 = h + (rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25)) + ((e & f) ^ (~e & g)) + K[i] + w[i];
    uint32_t t2 = (rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
    h = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
  }
  s.h[0] += a; s.h[1] += b; s.h[2] += c; s.h[3] += d; s.h[4] += e; s.h[5] += f; s.h[6] += g; s.h[7] += h;
}

void sha_init(Sha256 &s) {
  static const uint32_t iv[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                                 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
  memcpy(s.h, iv, sizeof(iv));
  s.block_len = 0;
  s.total_len = 0;
}

void sha_update(Sha256 &s, const uint8_t *p, size_t n) {
  s.total_len += n;
  while (n--) {
    s.block[s.block_len++] = *p++;
    if (s.block_len == 64) {
      compress(s, s.block);
      s.block_len = 0;
    }
  }
}

void sha_final(Sha256 &s, uint8_t out[32]) {
  uint64_t bits = s.total_len * 8;
  uint8_t pad = 0x80;
  sha_update(s, &pad, 1);
  uint8_t zero = 0;
  while (s.block_len != 56) sha_update(s, &zero, 1);
  uint8_t len[8];
  for (int i = 0; i < 8; i++) len[i] = (uint8_t)(bits >> (56 - 8 * i));
  sha_update(s, len, 8);
  for (int i = 0; i < 8; i++) {
    out[4 * i] = (uint8_t)(s.h[i] >> 24);
    out[4 * i + 1] = (uint8_t)(s.h[i] >> 16);
    out[4 * i + 2] = (uint8_t)(s.h[i] >> 8);
    out[4 * i + 3] = (uint8_t)s.h[i];
  }
}

uint64_t rng_state = 0x0123456789abcdefULL;

}  // namespace

namespace cb {

bool hmac_sha256(const uint8_t *key, size_t key_len, const Slice *parts, size_t n_parts, uint8_t out[kMacLen]) {
  uint8_t k[64] = {0};
  if (key_len > 64) {
    Sha256 s;
    sha_init(s);
    sha_update(s, key, key_len);
    sha_final(s, k);
  } else {
    memcpy(k, key, key_len);
  }
  uint8_t ipad[64], opad[64];
  for (int i = 0; i < 64; i++) {
    ipad[i] = k[i] ^ 0x36;
    opad[i] = k[i] ^ 0x5c;
  }
  uint8_t inner[32];
  Sha256 s;
  sha_init(s);
  sha_update(s, ipad, 64);
  for (size_t i = 0; i < n_parts; i++) sha_update(s, parts[i].data, parts[i].len);
  sha_final(s, inner);
  sha_init(s);
  sha_update(s, opad, 64);
  sha_update(s, inner, 32);
  sha_final(s, out);
  return true;
}

void random_bytes(uint8_t *out, size_t len) {
  for (size_t i = 0; i < len; i++) {
    rng_state = rng_state * 6364136223846793005ULL + 1442695040888963407ULL;  // 64-bit LCG
    out[i] = (uint8_t)(rng_state >> 56);
  }
}

bool sha256(const Slice *parts, size_t n_parts, uint8_t out[32]) {
  Sha256 s;
  sha_init(s);
  for (size_t i = 0; i < n_parts; i++) sha_update(s, parts[i].data, parts[i].len);
  sha_final(s, out);
  return true;
}

}  // namespace cb

// ---- RSA-3072 / RSA-PSS verification (RFC 8017, 9.1.2 and 8.1.2), reference for the PC tests only --------------
// Montgomery arithmetic on 96 little-endian 32-bit limbs. The parameters R^2 mod n and -n^-1 are computed here from
// n alone - the copies in the signature block are deliberately not trusted. Slow and simple; the device uses mbedtls.
namespace {

constexpr int L = 96;  // 3072 bit

int cmp(const uint32_t *a, const uint32_t *b) {
  for (int i = L - 1; i >= 0; i--)
    if (a[i] != b[i]) return a[i] < b[i] ? -1 : 1;
  return 0;
}

void sub_n(uint32_t *a, const uint32_t *n) {  // a -= n (mod 2^3072)
  int64_t borrow = 0;
  for (int i = 0; i < L; i++) {
    int64_t d = (int64_t)a[i] - n[i] - borrow;
    borrow = d < 0;
    a[i] = (uint32_t)d;
  }
}

// out = a * b * 2^-3072 mod n (CIOS). a, b < n.
void mont_mul(const uint32_t *a, const uint32_t *b, const uint32_t *n, uint32_t m0, uint32_t *out) {
  uint32_t t[L + 2] = {0};
  for (int i = 0; i < L; i++) {
    uint64_t c = 0;
    for (int j = 0; j < L; j++) {
      uint64_t s = (uint64_t)t[j] + (uint64_t)a[j] * b[i] + c;
      t[j] = (uint32_t)s;
      c = s >> 32;
    }
    uint64_t s = (uint64_t)t[L] + c;
    t[L] = (uint32_t)s;
    t[L + 1] = (uint32_t)(s >> 32);
    uint32_t m = t[0] * m0;
    s = (uint64_t)t[0] + (uint64_t)m * n[0];
    c = s >> 32;
    for (int j = 1; j < L; j++) {
      s = (uint64_t)t[j] + (uint64_t)m * n[j] + c;
      t[j - 1] = (uint32_t)s;
      c = s >> 32;
    }
    s = (uint64_t)t[L] + c;
    t[L - 1] = (uint32_t)s;
    t[L] = t[L + 1] + (uint32_t)(s >> 32);
    t[L + 1] = 0;
  }
  if (t[L] || cmp(t, n) >= 0) sub_n(t, n);
  memcpy(out, t, L * 4);
}

void load_le(const uint8_t *b, uint32_t *x) {
  for (int i = 0; i < L; i++)
    x[i] = (uint32_t)b[4 * i] | (uint32_t)b[4 * i + 1] << 8 | (uint32_t)b[4 * i + 2] << 16 | (uint32_t)b[4 * i + 3] << 24;
}

}  // namespace

namespace cb {

bool rsa3072_pss_verify(const uint8_t n_le[384], uint32_t e, const uint8_t digest[32], const uint8_t sig_le[384]) {
  uint32_t n[L], s[L];
  load_le(n_le, n);
  load_le(sig_le, s);
  if (!(n[L - 1] & 0x80000000u) || !(n[0] & 1) || e < 3 || !(e & 1) || cmp(s, n) >= 0) return false;
  uint32_t inv = n[0];  // Newton: inv = n0^-1 mod 2^32 (3 -> 6 -> 12 -> 24 -> 48 correct bits)
  for (int i = 0; i < 4; i++) inv *= 2 - n[0] * inv;
  uint32_t m0 = 0u - inv;
  uint32_t r2[L] = {1};  // R^2 mod n by 6144 modular doublings of 1
  for (int k = 0; k < 2 * 32 * L; k++) {
    uint32_t carry = 0;
    for (int i = 0; i < L; i++) {
      uint32_t nc = r2[i] >> 31;
      r2[i] = r2[i] << 1 | carry;
      carry = nc;
    }
    if (carry || cmp(r2, n) >= 0) sub_n(r2, n);
  }
  uint32_t sm[L], acc[L], one[L] = {1};
  mont_mul(s, r2, n, m0, sm);  // s in Montgomery form
  memcpy(acc, sm, sizeof acc);
  int top = 31;
  while (!(e >> top & 1)) top--;
  for (int bit = top - 1; bit >= 0; bit--) {
    mont_mul(acc, acc, n, m0, acc);
    if (e >> bit & 1) mont_mul(acc, sm, n, m0, acc);
  }
  mont_mul(acc, one, n, m0, acc);  // back to normal form: acc = s^e mod n

  uint8_t em[384];  // big endian
  for (int i = 0; i < 384; i++) em[383 - i] = (uint8_t)(acc[i / 4] >> (8 * (i % 4)));
  // EMSA-PSS-VERIFY, emBits = 3071, hLen = sLen = 32
  if (em[383] != 0xbc || (em[0] & 0x80)) return false;
  const uint8_t *h = em + 351;
  uint8_t db[351];
  for (uint32_t c = 0, off = 0; off < sizeof db; c++, off += 32) {  // MGF1-SHA-256
    uint8_t cnt[4] = {(uint8_t)(c >> 24), (uint8_t)(c >> 16), (uint8_t)(c >> 8), (uint8_t)c}, m[32];
    Slice p[2] = {{h, 32}, {cnt, 4}};
    sha256(p, 2, m);
    for (int i = 0; i < 32 && off + i < sizeof db; i++) db[off + i] = em[off + i] ^ m[i];
  }
  db[0] &= 0x7f;
  for (int i = 0; i < 318; i++)
    if (db[i]) return false;
  if (db[318] != 0x01) return false;
  static const uint8_t zeros[8] = {0};
  uint8_t h2[32];
  Slice p[3] = {{zeros, 8}, {digest, 32}, {db + 319, 32}};
  sha256(p, 3, h2);
  return memcmp(h, h2, 32) == 0;
}

}  // namespace cb
