// cb_bytes.h - explicit little-endian serialization helpers.
//
// The wire format is defined byte by byte (Softwarearchitektur 4.2, convention "explicit serialization
// instead of packed struct"): every multi-byte field is written and read through these helpers, so the
// layout does not depend on compiler padding or the CPU's byte order and can be checked in unit tests.
//
// Header-only, no platform dependencies: used on the ESP32-S3 and in the PC unit tests alike.
#pragma once

#include <cstddef>
#include <cstdint>

namespace cb {

inline void put_u16le(uint8_t *p, uint16_t v) {
  p[0] = (uint8_t)(v);
  p[1] = (uint8_t)(v >> 8);
}

inline void put_u32le(uint8_t *p, uint32_t v) {
  p[0] = (uint8_t)(v);
  p[1] = (uint8_t)(v >> 8);
  p[2] = (uint8_t)(v >> 16);
  p[3] = (uint8_t)(v >> 24);
}

inline uint16_t get_u16le(const uint8_t *p) {
  return (uint16_t)(p[0] | (p[1] << 8));
}

inline uint32_t get_u32le(const uint8_t *p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

// Compares two buffers in constant time: the run time depends only on `len`, never on where the first
// difference is. Used for every comparison of a secret-derived value (tags, nonces), so an attacker cannot
// learn a valid tag byte by byte from response timing (Softwarearchitektur 4.2, "Tag (konstante Laufzeit)").
inline bool ct_equal(const uint8_t *a, const uint8_t *b, size_t len) {
  uint8_t diff = 0;
  for (size_t i = 0; i < len; i++) diff |= (uint8_t)(a[i] ^ b[i]);
  return diff == 0;
}

// Wipes a secret (key, key stream, expected tag) with writes the compiler may not drop as "dead stores" - stack
// leftovers would otherwise end up in a core dump (security catalogue RV-60). Platform-neutral on purpose: cb_core
// also builds for the PC tests.
inline void secure_zero(void *p, size_t n) {
  volatile uint8_t *v = static_cast<volatile uint8_t *>(p);
  for (size_t i = 0; i < n; i++) v[i] = 0;
}

// Wipes a buffer when it goes out of scope - on every return path of a function.
struct Wipe {
  void *p;
  size_t n;
  ~Wipe() { secure_zero(p, n); }
};

}  // namespace cb
