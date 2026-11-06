// cb_crypto_esp.cpp - ESP32-S3 implementation of cb_crypto.h.
//
// HMAC through mbedtls, which on the S3 uses the SHA hardware accelerator. Random numbers from the
// hardware RNG (esp_fill_random): truly random while the radio is on, which is always the case when a
// nonce is created, because nonces only exist for network messages.
#include <mbedtls/md.h>
#include <esp_random.h>

#include "cb_crypto.h"

namespace cb {

bool hmac_sha256(const uint8_t *key, size_t key_len, const Slice *parts, size_t n_parts, uint8_t out[kMacLen]) {
  mbedtls_md_context_t ctx;
  mbedtls_md_init(&ctx);
  const mbedtls_md_info_t *info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
  bool ok = info && mbedtls_md_setup(&ctx, info, 1) == 0 && mbedtls_md_hmac_starts(&ctx, key, key_len) == 0;
  for (size_t i = 0; ok && i < n_parts; i++) ok = mbedtls_md_hmac_update(&ctx, parts[i].data, parts[i].len) == 0;
  ok = ok && mbedtls_md_hmac_finish(&ctx, out) == 0;
  mbedtls_md_free(&ctx);
  return ok;  // mbedtls only fails here on out-of-memory; callers then reject the frame (fail closed)
}

void random_bytes(uint8_t *out, size_t len) {
  esp_fill_random(out, len);
}

}  // namespace cb
