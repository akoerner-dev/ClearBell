// cb_update_sig.cpp - see cb_update_sig.h.
#include "cb_update_sig.h"

#include <cstring>

#include "cb_bytes.h"
#include "cb_crypto.h"
#include "cb_update_keys.h"
#include "cb_update_sig_vector.h"

namespace cb {

namespace {

constexpr uint8_t kMagic = 0xE7;
constexpr uint8_t kVersionRsa = 0x02;
constexpr size_t kOffDigest = 4;
constexpr size_t kOffKey = 36;
constexpr size_t kKeyPartLen = 384 + 4 + 384 + 4;  // n, e, rinv, m - exactly what espsecure digests
constexpr size_t kOffE = 36 + 384;
constexpr size_t kOffSig = 812;
constexpr size_t kOffCrc = 1196;

// CRC-32 as zlib.crc32 (IEEE 802.3, reflected, init and final xor 0xFFFFFFFF).
uint32_t crc32(const uint8_t *p, size_t n) {
  uint32_t c = 0xFFFFFFFFu;
  while (n--) {
    c ^= *p++;
    for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
  }
  return c ^ 0xFFFFFFFFu;
}

bool equal32(const uint8_t *a, const uint8_t *b) {
  uint8_t d = 0;  // constant time - not needed for public digests, but costs nothing
  for (size_t i = 0; i < 32; i++) d |= a[i] ^ b[i];
  return d == 0;
}

SigKey trusted_as(const uint8_t digest[kKeyDigestLen], const TrustedKeys &t) {
  if (t.has_owner && equal32(digest, t.owner)) return SigKey::Owner;
  if (t.owner_only) return SigKey::None;
  if (equal32(digest, t.main)) return SigKey::Main;
  if (equal32(digest, t.spare)) return SigKey::Spare;
  return SigKey::None;
}

int hex_nibble(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

}  // namespace

const char *sig_key_name(SigKey k) {
  switch (k) {
    case SigKey::None: return "-";
    case SigKey::Main: return "main key";
    case SigKey::Spare: return "spare key";
    case SigKey::Owner: return "owner key";
  }
  return "?";
}

const char *sig_check_name(SigCheck c) {
  switch (c) {
    case SigCheck::NoSignature: return "not signed";
    case SigCheck::BadBlock: return "signature block damaged or of an unsupported kind";
    case SigCheck::OtherFile: return "file changed after signing";
    case SigCheck::UnknownKey: return "signed with a key this unit does not trust";
    case SigCheck::BadSignature: return "signature invalid";
    case SigCheck::Ok: return "ok";
  }
  return "?";
}

TrustedKeys maker_trust() {
  TrustedKeys t{};
  memcpy(t.main, kMakerKeyDigest[0], kKeyDigestLen);
  memcpy(t.spare, kMakerKeyDigest[1], kKeyDigestLen);
  return t;
}

bool sig_block_key_digest(const uint8_t block[kSigBlockLen], uint8_t out[kKeyDigestLen]) {
  Slice s{block + kOffKey, kKeyPartLen};
  return sha256(&s, 1, out);
}

SigResult check_signature(const uint8_t sector[kSigSectorLen], const uint8_t image_digest[32], const TrustedKeys &t) {
  SigResult r;
  for (size_t i = 0; i < kSigBlocksMax; i++) {
    const uint8_t *b = sector + i * kSigBlockLen;
    if (b[0] != kMagic) break;  // espsecure fills the rest of the sector with 0xFF
    r.blocks++;
    SigCheck c;
    if (b[1] != kVersionRsa || crc32(b, kOffCrc) != get_u32le(b + kOffCrc)) {
      c = SigCheck::BadBlock;
    } else if (!equal32(b + kOffDigest, image_digest)) {
      c = SigCheck::OtherFile;
    } else {
      uint8_t kd[kKeyDigestLen];
      SigKey k = sig_block_key_digest(b, kd) ? trusted_as(kd, t) : SigKey::None;
      if (k == SigKey::None) {
        c = SigCheck::UnknownKey;
      } else if (rsa3072_pss_verify(b + kOffKey, get_u32le(b + kOffE), image_digest, b + kOffSig)) {
        r.check = SigCheck::Ok;
        r.key = k;
        return r;
      } else {
        c = SigCheck::BadSignature;
      }
    }
    if (c > r.check) r.check = c;  // keep the most telling reason
  }
  return r;
}

bool parse_key_digest(const char *hex, uint8_t out[kKeyDigestLen]) {
  if (!hex || strlen(hex) != 2 * kKeyDigestLen) return false;
  for (size_t i = 0; i < kKeyDigestLen; i++) {
    int hi = hex_nibble(hex[2 * i]), lo = hex_nibble(hex[2 * i + 1]);
    if (hi < 0 || lo < 0) return false;
    out[i] = (uint8_t)(hi << 4 | lo);
  }
  return true;
}

void key_fingerprint(const uint8_t d[kKeyDigestLen], char out[17]) {
  static const char hx[] = "0123456789abcdef";
  for (int i = 0; i < 8; i++) {
    out[2 * i] = hx[d[i] >> 4];
    out[2 * i + 1] = hx[d[i] & 15];
  }
  out[16] = 0;
}

int sig_selftest() {
  static uint8_t sector[kSigSectorLen];  // static: 4 KB is too much for the loop task's stack
  int failed = 0;

  // 1. The test image is a fixed 4096-byte pattern; its digest cross-checks sha256() against Python's hashlib.
  //    It is built in the sector buffer (two slices, to exercise the multi-part path) before that is reused.
  static_assert(kSigVecImageLen == kSigSectorLen, "the test image is built in the sector buffer");
  uint8_t digest[32];
  for (size_t i = 0; i < kSigVecImageLen; i++) sector[i] = sig_vec_image_byte(i);
  Slice parts[2] = {{sector, 1000}, {sector + 1000, kSigVecImageLen - 1000}};
  if (!sha256(parts, 2, digest) || !equal32(digest, kSigVecImageDigest)) failed++;

  auto reset = [&] {
    memset(sector, 0xFF, sizeof sector);
    memcpy(sector, kSigVecBlocks, sizeof kSigVecBlocks);
  };
  auto keys = [](const uint8_t *main, const uint8_t *spare) {
    TrustedKeys t{};
    if (main) memcpy(t.main, main, 32);
    if (spare) memcpy(t.spare, spare, 32);
    return t;
  };
  static const uint8_t none[32] = {0};

  // 2. Block 1 (key A) trusted as main key.
  reset();
  SigResult r = check_signature(sector, kSigVecImageDigest, keys(kSigVecKeyA, none));
  if (r.check != SigCheck::Ok || r.key != SigKey::Main || r.blocks != 1) failed++;  // stops at the first valid block
  // 3. Only key B trusted, as spare: block 1 is skipped, block 2 verifies.
  r = check_signature(sector, kSigVecImageDigest, keys(none, kSigVecKeyB));
  if (r.check != SigCheck::Ok || r.key != SigKey::Spare) failed++;
  // 4. Neither key trusted.
  r = check_signature(sector, kSigVecImageDigest, keys(none, none));
  if (r.check != SigCheck::UnknownKey) failed++;
  // 5. Owner key A with "owner only": accepted as owner; the manufacturer key B no longer counts.
  TrustedKeys o = keys(kSigVecKeyB, none);
  memcpy(o.owner, kSigVecKeyA, 32);
  o.has_owner = o.owner_only = true;
  r = check_signature(sector, kSigVecImageDigest, o);
  if (r.check != SigCheck::Ok || r.key != SigKey::Owner) failed++;
  memcpy(o.owner, none, 32);  // owner key that signed nothing, still "owner only": B must not help
  r = check_signature(sector, kSigVecImageDigest, o);
  if (r.check != SigCheck::UnknownKey) failed++;
  // 6. One bit of the signature flipped (CRC repaired, so it reaches the RSA check).
  sector[kOffSig + 100] ^= 0x01;
  put_u32le(sector + kOffCrc, crc32(sector, kOffCrc));
  r = check_signature(sector, kSigVecImageDigest, keys(kSigVecKeyA, none));
  if (r.check != SigCheck::BadSignature) failed++;
  // 7. File changed after signing: another digest.
  reset();
  memcpy(digest, kSigVecImageDigest, 32);
  digest[0] ^= 0x80;
  r = check_signature(sector, digest, keys(kSigVecKeyA, kSigVecKeyB));
  if (r.check != SigCheck::OtherFile) failed++;
  // 8. Block damaged (CRC no longer matches).
  sector[kOffKey + 3] ^= 0x10;
  r = check_signature(sector, kSigVecImageDigest, keys(kSigVecKeyA, none));
  if (r.check != SigCheck::UnknownKey) failed++;  // block 1 damaged, block 2 intact but key B untrusted
  // 9. Unsigned file.
  memset(sector, 0xFF, sizeof sector);
  r = check_signature(sector, kSigVecImageDigest, keys(kSigVecKeyA, kSigVecKeyB));
  if (r.check != SigCheck::NoSignature || r.blocks != 0) failed++;
  return failed;
}

}  // namespace cb
