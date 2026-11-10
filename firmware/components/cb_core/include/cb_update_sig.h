// cb_update_sig.h - signed firmware updates (port step 10d, decision R26), hardware-free, PC-tested.
//
// WHY: an update replaces the firmware of a unit that opens the house door. The signature proves that a file comes
// from whoever holds a trusted key - so a manipulated file (fake download page, a neighbour with the password) is
// refused. It does NOT protect against someone holding the circuit board (USB flashing; that is hardening stage C).
//
// FORMAT (the one of ESP-IDF Secure Boot v2, so stage C can later use the very same keys): the build output is
// padded with 0xFF to a multiple of 4096 bytes, then ONE 4096-byte signature sector is appended. It holds up to three
// 1216-byte RSA-3072 blocks (rest 0xFF), written by `espsecure.py sign_data --version 2` (tools/cb.py signieren):
//
//   off  len  content
//   0    1    magic 0xE7
//   1    1    version 0x02 = RSA-3072 / RSA-PSS / SHA-256 (0x03 = ECDSA: not supported, skipped)
//   4    32   SHA-256 of everything before the signature sector ("image digest")
//   36   384  modulus n, little endian            +
//   420  4    public exponent e                    |  the "key" part; its SHA-256 is the KEY DIGEST that
//   424  384  R^2 mod n, little endian (unused)     |  identifies a key (= what stage C burns into the eFuse)
//   808  4    -n^-1 mod 2^32 (unused)             +
//   812  384  RSA-PSS signature (salt 32, MGF1-SHA-256), little endian
//   1196 4    CRC-32 (zlib) over bytes 0..1195
//   1200 16   zero padding
//
// TRUST (R26, Meister 10.11.2026): every firmware knows two manufacturer key digests (cb_update_keys.h: main key,
// spare key kept offline). The buyer may add ONE own key in maintenance mode (physical presence + device password)
// and may even stop trusting the manufacturer keys - so nobody is ever stuck with a dead manufacturer (and the
// firmware stays replaceable, LGPL). A file is accepted when at least ONE block is intact, belongs to THIS file, has
// a trusted key and a valid signature. The RSA check uses the digest computed from the received bytes, never the
// digest written in the block.
#pragma once

#include <cstddef>
#include <cstdint>

namespace cb {

constexpr size_t kSigSectorLen = 4096;
constexpr size_t kSigBlockLen = 1216;
constexpr size_t kSigBlocksMax = 3;
constexpr size_t kKeyDigestLen = 32;
constexpr size_t kRsaLen = 384;  // RSA-3072

enum class SigKey : uint8_t { None, Main, Spare, Owner };
const char *sig_key_name(SigKey k);

// Which keys a unit trusts at the moment.
struct TrustedKeys {
  uint8_t main[kKeyDigestLen] = {};
  uint8_t spare[kKeyDigestLen] = {};
  uint8_t owner[kKeyDigestLen] = {};
  bool has_owner = false;
  bool owner_only = false;  // buyer's choice: manufacturer keys no longer accepted
};

// The two manufacturer keys from cb_update_keys.h, no owner key.
TrustedKeys maker_trust();

// Ordered from "nothing there" to "valid": a refused file reports the most telling reason found in any block.
enum class SigCheck : uint8_t {
  NoSignature,   // no signature block at all (an unsigned build output)
  BadBlock,      // block damaged (CRC) or of an unsupported kind (ECDSA)
  OtherFile,     // the block was made for another file - the file was changed after signing
  UnknownKey,    // intact block for this file, but the key is not trusted by this unit
  BadSignature,  // trusted key, but the signature does not verify - forged or broken
  Ok,
};
const char *sig_check_name(SigCheck c);

struct SigResult {
  SigCheck check = SigCheck::NoSignature;
  SigKey key = SigKey::None;  // the key that verified (check == Ok)
  int blocks = 0;             // blocks with the right magic
};

// sector: the last 4096 bytes of the upload. image_digest: SHA-256 of all bytes before it, as received.
SigResult check_signature(const uint8_t sector[kSigSectorLen], const uint8_t image_digest[32], const TrustedKeys &t);

// SHA-256 over the key part of a block (bytes 36..811) - the key digest. False if the hash failed.
bool sig_block_key_digest(const uint8_t block[kSigBlockLen], uint8_t out[kKeyDigestLen]);

// "a1b2..." (64 hex digits, upper or lower case) -> 32 bytes. False for anything else.
bool parse_key_digest(const char *hex, uint8_t out[kKeyDigestLen]);
// First 8 bytes as 16 lower-case hex digits: enough to tell keys apart on the status page.
void key_fingerprint(const uint8_t d[kKeyDigestLen], char out[17]);

// Known-answer test with a vector made by espsecure from two throwaway test keys (cb_update_sig_vector.h):
// valid via either block, refused when tampered or untrusted. Returns the number of failed cases. Shared by the PC
// tests and the on-device self-test, so the device's RSA implementation is checked on the real hardware.
int sig_selftest();

}  // namespace cb
