// cb_pairing_logic.cpp - see cb_pairing_logic.h.
#include "cb_pairing_logic.h"

#include <cstdio>
#include <cstring>

#include "cb_bytes.h"
#include "cb_crypto.h"

namespace cb {

namespace {

const uint8_t *u8(const char *s) { return reinterpret_cast<const uint8_t *>(s); }

bool derive(const char *code, uint8_t k[32]) {
  Slice p[] = {{u8("CBP1"), 4}};
  return hmac_sha256(u8(code), strlen(code), p, 1, k);
}

bool stream(const uint8_t k[32], const char *label, const uint8_t nonce[kPairNonceLen], uint8_t out[32]) {
  Slice p[] = {{u8(label), strlen(label)}, {nonce, kPairNonceLen}};
  return hmac_sha256(k, 32, p, 2, out);
}

bool offer_tag(const uint8_t k[32], const char *out_ser, const char *in_ser, const uint8_t *nonce, const uint8_t *enc,
               uint8_t tag[32]) {
  Slice p[] = {{u8("tag|"), 4},       {u8(out_ser), strlen(out_ser)}, {u8("|"), 1}, {u8(in_ser), strlen(in_ser)},
               {nonce, kPairNonceLen}, {enc, 32}};
  return hmac_sha256(k, 32, p, 6, tag);
}

}  // namespace

bool pair_seal(const char *code, const char *out_ser, const char *in_ser, const uint8_t link_key[32],
               const uint8_t nonce[kPairNonceLen], PairOffer *o) {
  uint8_t k[32], s[32];
  if (!derive(code, k) || !stream(k, "enc", nonce, s)) return false;
  memcpy(o->nonce, nonce, kPairNonceLen);
  for (int i = 0; i < 32; i++) o->enc[i] = link_key[i] ^ s[i];
  return offer_tag(k, out_ser, in_ser, o->nonce, o->enc, o->tag);
}

bool pair_open(const char *code, const char *out_ser, const char *in_ser, const PairOffer &in, uint8_t key_out[32]) {
  uint8_t k[32], s[32], tag[32];
  if (!derive(code, k) || !offer_tag(k, out_ser, in_ser, in.nonce, in.enc, tag)) return false;
  if (!ct_equal(tag, in.tag, 32)) return false;
  if (!stream(k, "enc", in.nonce, s)) return false;
  for (int i = 0; i < 32; i++) key_out[i] = in.enc[i] ^ s[i];
  return true;
}

bool pair_confirm(const uint8_t link_key[32], const uint8_t nonce[kPairNonceLen], uint8_t flat, uint8_t out[32]) {
  Slice p[] = {{u8("confirm|"), 8}, {nonce, kPairNonceLen}, {&flat, 1}};
  return hmac_sha256(link_key, 32, p, 3, out);
}

bool wrap32(const uint8_t key[32], const char *label, const uint8_t nonce[kPairNonceLen], const uint8_t *plain,
            size_t n, uint8_t *enc, uint8_t tag[16]) {
  if (n > 32) return false;
  uint8_t s[32], t[32];
  if (!stream(key, label, nonce, s)) return false;
  for (size_t i = 0; i < n; i++) enc[i] = plain[i] ^ s[i];
  Slice p[] = {{u8("wrap|"), 5}, {u8(label), strlen(label)}, {nonce, kPairNonceLen}, {enc, n}};
  if (!hmac_sha256(key, 32, p, 4, t)) return false;
  memcpy(tag, t, 16);
  return true;
}

bool unwrap32(const uint8_t key[32], const char *label, const uint8_t nonce[kPairNonceLen], const uint8_t *enc,
              size_t n, const uint8_t tag[16], uint8_t *plain) {
  if (n > 32) return false;
  uint8_t s[32], t[32];
  Slice p[] = {{u8("wrap|"), 5}, {u8(label), strlen(label)}, {nonce, kPairNonceLen}, {enc, n}};
  if (!hmac_sha256(key, 32, p, 4, t) || !ct_equal(t, tag, 16)) return false;
  if (!stream(key, label, nonce, s)) return false;
  for (size_t i = 0; i < n; i++) plain[i] = enc[i] ^ s[i];
  return true;
}

bool crypt32(const uint8_t key[32], const char *label, const uint8_t nonce[kPairNonceLen], const uint8_t *in,
             size_t n, uint8_t *out) {
  if (n > 32) return false;
  uint8_t s[32];
  if (!stream(key, label, nonce, s)) return false;
  for (size_t i = 0; i < n; i++) out[i] = in[i] ^ s[i];
  return true;
}

void to_hex_lower(const uint8_t *in, size_t n, char *out) {
  static const char h[] = "0123456789abcdef";
  for (size_t i = 0; i < n; i++) {
    out[2 * i] = h[in[i] >> 4];
    out[2 * i + 1] = h[in[i] & 15];
  }
  out[2 * n] = 0;
}

bool from_hex(const char *in, uint8_t *out, size_t n) {
  if (!in || strlen(in) != 2 * n) return false;
  for (size_t i = 0; i < n; i++) {
    int v = 0;
    for (int k = 0; k < 2; k++) {
      char c = in[2 * i + k];
      int d = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
      if (d < 0) return false;
      v = v * 16 + d;
    }
    out[i] = (uint8_t)v;
  }
  return true;
}

}  // namespace cb
