// cb_crypto_esp.cpp - ESP32-S3 implementation of cb_crypto.h.
//
// HMAC and SHA-256 through mbedtls, which on the S3 uses the SHA hardware accelerator; RSA through mbedtls with the
// hardware MPI unit. Random numbers from the hardware RNG (esp_fill_random): truly random while the radio is on,
// which is always the case when a nonce is created, because nonces only exist for network messages.
#include <mbedtls/md.h>
#include <mbedtls/rsa.h>
#include <mbedtls/sha256.h>
#include <esp_random.h>

#include <cstdlib>

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

bool sha256(const Slice *parts, size_t n_parts, uint8_t out[32]) {
  mbedtls_sha256_context ctx;
  mbedtls_sha256_init(&ctx);
  bool ok = mbedtls_sha256_starts(&ctx, 0) == 0;
  for (size_t i = 0; ok && i < n_parts; i++) ok = mbedtls_sha256_update(&ctx, parts[i].data, parts[i].len) == 0;
  ok = ok && mbedtls_sha256_finish(&ctx, out) == 0;
  mbedtls_sha256_free(&ctx);
  return ok;
}

bool rsa3072_pss_verify(const uint8_t n_le[384], uint32_t e, const uint8_t digest[32], const uint8_t sig_le[384]) {
  // Big-endian copies on the heap (the web task that receives updates has a small stack).
  uint8_t *buf = static_cast<uint8_t *>(malloc(2 * 384));
  if (!buf) return false;
  uint8_t *n_be = buf, *s_be = buf + 384;
  for (int i = 0; i < 384; i++) {
    n_be[i] = n_le[383 - i];
    s_be[i] = sig_le[383 - i];
  }
  mbedtls_rsa_context rsa;
  mbedtls_mpi N, E;
  mbedtls_rsa_init(&rsa);
  mbedtls_mpi_init(&N);
  mbedtls_mpi_init(&E);
  bool ok = (n_be[0] & 0x80) && mbedtls_mpi_read_binary(&N, n_be, 384) == 0 && mbedtls_mpi_lset(&E, e) == 0 &&
            mbedtls_rsa_import(&rsa, &N, nullptr, nullptr, nullptr, &E) == 0 && mbedtls_rsa_complete(&rsa) == 0 &&
            mbedtls_rsa_check_pubkey(&rsa) == 0 && mbedtls_rsa_get_len(&rsa) == 384 &&
            mbedtls_rsa_set_padding(&rsa, MBEDTLS_RSA_PKCS_V21, MBEDTLS_MD_SHA256) == 0 &&
            mbedtls_rsa_rsassa_pss_verify_ext(&rsa, MBEDTLS_MD_SHA256, 32, digest, MBEDTLS_MD_SHA256, 32, s_be) == 0;
  mbedtls_mpi_free(&N);
  mbedtls_mpi_free(&E);
  mbedtls_rsa_free(&rsa);
  free(buf);
  return ok;
}

}  // namespace cb
