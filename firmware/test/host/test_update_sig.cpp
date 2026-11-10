// test_update_sig.cpp - port step 10d: signed updates and downgrade protection (cb_update_sig.h, R26).
#include <cstring>

#include "cb_crypto.h"
#include "cb_update_keys.h"
#include "cb_update_logic.h"
#include "cb_update_sig.h"
#include "cb_update_sig_vector.h"
#include "mini_test.h"

using namespace cb;

namespace {

uint8_t sector[kSigSectorLen];

void fresh_sector() {
  memset(sector, 0xFF, sizeof sector);
  memcpy(sector, kSigVecBlocks, sizeof kSigVecBlocks);
}

TrustedKeys trust(const uint8_t *main, const uint8_t *spare) {
  TrustedKeys t{};
  if (main) memcpy(t.main, main, 32);
  if (spare) memcpy(t.spare, spare, 32);
  return t;
}

}  // namespace

TEST(sig_selftest_vector_from_espsecure) {
  // The same known-answer cases the device runs at boot - here against the PC reference RSA.
  CHECK_EQ(sig_selftest(), 0);
}

TEST(sig_rsa_reference_rejects_tampering) {
  fresh_sector();
  const uint8_t *n = sector + 36, *sig = sector + 812;
  CHECK(rsa3072_pss_verify(n, 65537, kSigVecImageDigest, sig));
  uint8_t d[32];
  memcpy(d, kSigVecImageDigest, 32);
  d[31] ^= 1;
  CHECK(!rsa3072_pss_verify(n, 65537, d, sig));            // other message
  CHECK(!rsa3072_pss_verify(n, 3, kSigVecImageDigest, sig));  // other exponent
  CHECK(!rsa3072_pss_verify(sector + kSigBlockLen + 36, 65537, kSigVecImageDigest, sig));  // other key (B)
  uint8_t big[384];
  memset(big, 0xFF, sizeof big);  // signature >= n
  CHECK(!rsa3072_pss_verify(n, 65537, kSigVecImageDigest, big));
}

TEST(sig_key_digest_is_espsecure_digest) {
  fresh_sector();
  uint8_t kd[32];
  CHECK(sig_block_key_digest(sector, kd));
  CHECK(memcmp(kd, kSigVecKeyA, 32) == 0);
  CHECK(sig_block_key_digest(sector + kSigBlockLen, kd));
  CHECK(memcmp(kd, kSigVecKeyB, 32) == 0);
}

TEST(sig_trust_rules) {
  fresh_sector();
  static const uint8_t none[32] = {0};
  SigResult r = check_signature(sector, kSigVecImageDigest, trust(kSigVecKeyB, kSigVecKeyA));
  CHECK(r.check == SigCheck::Ok);
  CHECK(r.key == SigKey::Spare);  // block 1 (key A) verifies first - A is the spare key here
  // Owner key alone, manufacturer keys unknown to the file: accepted as owner.
  TrustedKeys o = trust(none, none);
  memcpy(o.owner, kSigVecKeyB, 32);
  o.has_owner = true;
  r = check_signature(sector, kSigVecImageDigest, o);
  CHECK(r.check == SigCheck::Ok);
  CHECK(r.key == SigKey::Owner);
  // has_owner = false: the stored bytes must not count.
  o.has_owner = false;
  CHECK(check_signature(sector, kSigVecImageDigest, o).check == SigCheck::UnknownKey);
  // Only the first three blocks are looked at, and an ECDSA block (version 3) is skipped, not trusted.
  fresh_sector();
  sector[1] = 0x03;
  r = check_signature(sector, kSigVecImageDigest, trust(kSigVecKeyA, none));
  CHECK(r.check == SigCheck::UnknownKey);  // block 1 unsupported (BadBlock), block 2 untrusted (higher)
  r = check_signature(sector, kSigVecImageDigest, trust(kSigVecKeyA, kSigVecKeyB));
  CHECK(r.check == SigCheck::Ok);
  CHECK(r.key == SigKey::Spare);
}

TEST(sig_maker_keys_are_real_and_distinct) {
  TrustedKeys t = maker_trust();
  static const uint8_t none[32] = {0};
  CHECK(memcmp(t.main, none, 32) != 0);
  CHECK(memcmp(t.spare, none, 32) != 0);
  CHECK(memcmp(t.main, t.spare, 32) != 0);
  CHECK(!t.has_owner);
  CHECK(!t.owner_only);
  // The throwaway test keys must never be trusted by a real firmware.
  CHECK(memcmp(t.main, kSigVecKeyA, 32) != 0 && memcmp(t.main, kSigVecKeyB, 32) != 0);
  CHECK(memcmp(t.spare, kSigVecKeyA, 32) != 0 && memcmp(t.spare, kSigVecKeyB, 32) != 0);
  fresh_sector();
  CHECK(check_signature(sector, kSigVecImageDigest, t).check == SigCheck::UnknownKey);
}

TEST(sig_key_digest_text) {
  uint8_t d[32];
  CHECK(parse_key_digest("4A0129E08BDAC2389D1B4A58862442DF74FAE1C38A1C6D222A193A437E14AC80", d));
  CHECK(d[0] == 0x4a && d[31] == 0x80);
  char fp[17];
  key_fingerprint(d, fp);
  CHECK(strcmp(fp, "4a0129e08bdac238") == 0);
  CHECK(!parse_key_digest("4a0129", d));
  CHECK(!parse_key_digest("zz0129e08bdac2389d1b4a58862442df74fae1c38a1c6d222a193a437e14ac80", d));
  CHECK(!parse_key_digest(nullptr, d));
}

TEST(update_version_older) {
  CHECK(!version_older("0.2.0-dev+aussen", "0.2.0-dev+aussen"));  // reinstall the same
  CHECK(!version_older("0.2.1+aussen", "0.2.0+aussen"));
  CHECK(version_older("0.1.9+aussen", "0.2.0+aussen"));
  CHECK(version_older("0.2.0-dev+innen", "0.2.0+innen"));       // pre-release is older than the release
  CHECK(!version_older("0.2.0+innen", "0.2.0-dev+innen"));
  CHECK(version_older("0.2.0-alpha", "0.2.0-beta"));
  CHECK(!version_older("0.10.0", "0.9.9"));                      // numbers, not text
  CHECK(version_older("1.0.0", "1.0.10"));
  CHECK(version_older("x.y.z+innen", "0.2.0+innen"));            // unreadable candidate: needs "allow older"
  CHECK(version_older("0.2+innen", "0.2.0+innen"));
  CHECK(!version_older("0.2.0+innen", "kaputt"));                // unreadable own version never blocks
  CHECK(version_older(nullptr, "0.2.0"));
}
