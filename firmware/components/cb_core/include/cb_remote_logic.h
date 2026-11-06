// cb_remote_logic.h - remote opening by phone (Softwarearchitektur 4.5), hardware-free, PC-tested.
//
// The use case: a child stands locked out at the door, no adult is home. Flow:
//   1. ring at EG -> the outdoor unit draws a one-time token (128 bit, valid kTokenMs) and sends a push via
//      ntfy.sh whose button opens the phone app with the token
//   2. the phone signs:   HMAC-SHA256(key = phone key as text, msg = "CBR1|" + token + "|" + phone id)
//      and publishes      "CBR1 <token> <phone id> <signature hex>"   to the command topic
//   3. the outdoor unit reads the command stream, checks scheme, phone, token and signature, and only then asks
//      the door task to open; after an opening ALL tokens are gone
//
// Why a phone signature: ntfy.sh topics are public ("the topic is essentially a password") and keep messages
// 12 h. The push contains the token, so whoever reads the topic HAS the token - it is worthless without the
// phone key, which lives only on that phone and in the outdoor unit. This also takes the service operator out
// of the chain of trust. The token makes a recorded signed answer useless after its one opening and after
// kTokenMs; tokens live in RAM only, so a reboot invalidates all of them.
//
// The key is used AS TEXT (its 64 hex characters), because every phone tool can HMAC with a text key
// (HTTP Shortcuts on Android: hmac('SHA-256', key, message)) - 64 hex characters still carry 256 bit.
// "CBR1" is the scheme id: a second scheme (e.g. for iOS) can be added without breaking Android.
#pragma once

#include <cstddef>
#include <cstdint>

namespace cb {

constexpr uint32_t kRemoteTokenMs = 5 * 60 * 1000;  // draft (SA 4.5: "5 min are acceptable"), fixed in bring-up
constexpr int kRemoteTokens = 3;                    // the newest 3 rings stay usable (visitor rings twice)
constexpr int kRemotePhones = 4;
constexpr size_t kPhoneIdMax = 8;                   // phone id: 1..8 of [A-Za-z0-9-]
constexpr size_t kPhoneKeyHex = 64;

enum class RemoteResult : uint8_t {
  Ok,
  Malformed,     // not "<scheme> <token> <phone> <sig>" with the right lengths and characters
  UnknownScheme,
  UnknownPhone,  // no such phone id provisioned (or revoked)
  UnknownToken,  // never issued, already used, or from before a reboot
  Expired,
  BadSignature,
};
const char *remote_result_name(RemoteResult r);

class RemoteAuth {
 public:
  // Phone keys from provisioning. key_hex: 64 hex characters. Returns false for an invalid id or key.
  bool set_phone(int slot, const char *id, const char *key_hex);
  int phone_count() const;
  // At runtime (port step 10c): add or replace by id -> slot used, -1 if full/invalid. remove -> false if unknown.
  int add_phone(const char *id, const char *key_hex);
  bool remove_phone(const char *id);

  // New ring at EG: draws a token (hardware RNG on the device) and returns it as 32 hex characters.
  void issue(uint32_t now_ms, char token_out[33]);

  // Checks a command message. Does NOT use up the token - call consume_all() once the door really opened,
  // so a refusal (lock-out time) leaves the token usable for a second try.
  RemoteResult check(const char *msg, uint32_t now_ms, char phone_out[kPhoneIdMax + 1]) const;
  void consume_all();
  int active(uint32_t now_ms) const;

  // The phone side, for tests and the PC phone simulator: signature over token and phone id.
  static bool sign(const char *key_hex, const char *token, const char *phone, char out[65]);

 private:
  struct Token {
    char hex[33];
    uint32_t issued;
    bool used;
  } tokens_[kRemoteTokens] = {};
  int next_ = 0;
  struct Phone {
    char id[kPhoneIdMax + 1];
    char key[kPhoneKeyHex + 1];
    bool used;
  } phones_[kRemotePhones] = {};
};

// Reads the string value of "key" from one JSON object (one line of the ntfy stream). Handles escapes
// (\" \\ \/ \n \t \uXXXX as '?'), returns false if the key is missing, not a string, or does not fit.
// Deliberately tiny: the ntfy lines are flat, and a parser that cannot fit a value refuses it.
bool json_get_string(const char *json, const char *key, char *out, size_t cap);

}  // namespace cb
