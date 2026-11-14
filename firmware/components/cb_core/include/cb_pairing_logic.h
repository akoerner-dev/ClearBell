// cb_pairing_logic.h - pairing an indoor unit with the outdoor unit by the indoor unit's DEVICE CODE (port step
// 10b), and wrapping secrets for the link (10c). Hardware-free, PC-tested.
//
// Buyer's view: on the outdoor unit's page "Inneneinheit koppeln", choose the indoor unit found in the network and
// type the code from ITS label. Why that is enough, and safe:
//   - The outdoor unit draws the link key (256 bit) and sends it to the indoor unit over the home network,
//     encrypted and authenticated with a key derived from the indoor unit's device code:
//        K      = HMAC-SHA256(code, "CBP1")
//        enc    = link_key XOR HMAC-SHA256(K, "enc" | nonce)            (one-time pad from a PRF, fresh nonce)
//        tag    = HMAC-SHA256(K, "tag" | outdoor no. | indoor no. | nonce | enc)
//   - The indoor unit accepts only with the right tag, then proves it holds the key:
//        confirm = HMAC-SHA256(link_key, "confirm" | nonce | flat)
//   - Whoever does not know the indoor code (it is on the unit inside the flat) can neither inject a key nor read
//     one - not even the other household in the same WiFi (attacker F). 80 bit of code cannot be guessed offline
//     from a recorded exchange, and the indoor unit locks after a few wrong attempts.
//
// The same construction with the LINK key as K wraps a phone key for the trip indoor -> outdoor (10c), because
// protocol v2 authenticates frames but does not encrypt them.
#pragma once

#include <cstddef>
#include <cstdint>

namespace cb {

constexpr size_t kPairNonceLen = 16;

struct PairOffer {
  uint8_t nonce[kPairNonceLen];
  uint8_t enc[32];
  uint8_t tag[32];
};

// Outdoor side. code: the 16 canonical characters (device_code_normalize). Serials: 6 hex characters each.
bool pair_seal(const char *code, const char *outdoor_serial, const char *indoor_serial, const uint8_t link_key[32],
               const uint8_t nonce[kPairNonceLen], PairOffer *out);
// Indoor side: false if the tag does not fit (wrong code, other unit, tampered). key_out only valid on true.
bool pair_open(const char *code, const char *outdoor_serial, const char *indoor_serial, const PairOffer &in,
               uint8_t key_out[32]);
// Key confirmation of the indoor unit, binding the flat it serves (2 = EG, 3 = OG).
bool pair_confirm(const uint8_t link_key[32], const uint8_t nonce[kPairNonceLen], uint8_t flat, uint8_t out[32]);

// Wrap / unwrap up to 32 bytes with a 32-byte key (phone key over the link, 10c). Tag: 16 bytes.
bool wrap32(const uint8_t key[32], const char *label, const uint8_t nonce[kPairNonceLen], const uint8_t *plain,
            size_t n, uint8_t *enc, uint8_t tag[16]);
bool unwrap32(const uint8_t key[32], const char *label, const uint8_t nonce[kPairNonceLen], const uint8_t *enc,
              size_t n, const uint8_t tag[16], uint8_t *plain);

// Stream encryption without tag, for data inside an already authenticated frame (phone key in an Event, 10c).
bool crypt32(const uint8_t key[32], const char *label, const uint8_t nonce[kPairNonceLen], const uint8_t *in,
             size_t n, uint8_t *out);

// (remote_names() - topics derived from the LINK key - is gone since R27: phone topics come from each phone's own
// key, so re-pairing does not invalidate them by itself. VK-01: unpairing/re-pairing deletes the flat's phones.)

// Hex helpers for the HTTP form fields.
void to_hex_lower(const uint8_t *in, size_t n, char *out);  // out: 2n+1
bool from_hex(const char *in, uint8_t *out, size_t n);       // exactly 2n hex characters

}  // namespace cb
