// cb_crypto.h - the two cryptographic primitives the protocol needs, behind a platform-neutral interface.
//
// The protocol code (cb_protocol, cb_link) only calls these functions; which implementation is linked
// decides where they run:
//   ESP32-S3:      src/cb_crypto_esp.cpp  - mbedtls (hardware SHA accelerator) and the hardware RNG
//   PC unit tests: test/host/crypto_host.cpp - portable reference SHA-256, deterministic test RNG
// The RFC 4231 test vectors run against BOTH: on the PC as unit test, on the device as self-test at boot
// (console command "selftest"), so a broken platform implementation is caught on the real hardware too.
#pragma once

#include <cstddef>
#include <cstdint>

namespace cb {

constexpr size_t kKeyLen = 32;   // length of every link key (HMAC-SHA256, 256 bit)
constexpr size_t kMacLen = 32;   // full HMAC-SHA256 output

// A read-only piece of a message. hmac_sha256() authenticates the concatenation of several slices, so
// a frame can be authenticated without first copying header, payload and domain prefix into one buffer.
struct Slice {
  const uint8_t *data;
  size_t len;
};

// out = HMAC-SHA256(key, parts[0] || parts[1] || ... || parts[n_parts-1]). Returns false if the
// computation failed (device: out of memory); `out` is then undefined and the caller must FAIL CLOSED,
// i.e. treat the frame as invalid. Never compare against `out` after a false return.
bool hmac_sha256(const uint8_t *key, size_t key_len, const Slice *parts, size_t n_parts, uint8_t out[kMacLen]);

// Fills `out` with unpredictable bytes. On the ESP32-S3 this is the hardware RNG, which is truly random
// while WiFi or Bluetooth is running (Softwarearchitektur 4.4) - the units only create nonces with WiFi on.
void random_bytes(uint8_t *out, size_t len);

// Runs the RFC 4231 HMAC-SHA256 test cases 1-4, 6 and 7 against hmac_sha256(). Returns the number of
// failed cases (0 = all good). Shared by the PC tests and the on-device self-test.
int hmac_selftest();

}  // namespace cb
