// cb_http_auth.h - device password for the status page: HTTP Digest authentication (RFC 7616, MD5).
// Hardware-free, PC-tested (test/host/test_status.cpp).
//
// Why Digest and not Basic: the status page runs over plain HTTP in the house WiFi (Softwarearchitektur
// 8.3). With Basic the password would cross the WiFi in clear text, readable by every device that knows the
// WiFi key - i.e. also by the other household (attacker F, SA 7.2). With Digest only a hash bound to a
// one-time server nonce crosses the air. MD5 is the one algorithm every browser speaks; its weakness does not
// matter here because the device password is random (96 bit, tools/provisionieren.py), so guessing it from a
// recorded exchange is infeasible.
//
// Protection built in:
//   - nonces are stateless (time + sequence number + HMAC with a secret drawn at boot), valid 10 min;
//     a reboot invalidates all of them ("stale" -> the browser asks again silently, no password prompt)
//   - nonce count must grow per nonce -> a recorded request cannot be replayed (matters for uploads)
//   - the `uri` in the header must be the requested URI -> an answer for one page does not open another
//   - 5 wrong passwords in a row -> 30 s lock for everybody (slows guessing down to ~10 tries per minute)
#pragma once

#include <cstddef>
#include <cstdint>

namespace cb {

// ---- MD5 (RFC 1321), portable, only for Digest ----------------------------------------------------------
struct Md5 {
  uint32_t a, b, c, d;
  uint8_t block[64];
  size_t block_len;
  uint64_t total;
};
void md5_init(Md5 *m);
void md5_update(Md5 *m, const void *data, size_t len);
void md5_final(Md5 *m, uint8_t out[16]);
void md5_hex(const char *text, char out[33]);  // convenience for tests

// ---- Header parsing ---------------------------------------------------------------------------------------
struct DigestFields {
  char username[33];
  char realm[48];
  char nonce[48];
  char uri[160];
  char response[33];
  char qop[8];
  char nc[9];
  char cnonce[65];
  char algorithm[12];
};
// Parses the value of an "Authorization: Digest ..." header. False if it is not Digest, a field is too long,
// or a mandatory field is missing. Quoted and unquoted values, any order, unknown fields ignored.
bool parse_digest(const char *header, DigestFields *out);

// The expected `response` for qop=auth: MD5(MD5(user:realm:pass):nonce:nc:cnonce:auth:MD5(method:uri)).
void digest_response(const DigestFields &f, const char *password, const char *method, char out[33]);

// ---- Own password (Produktreife 8, Meister 08.11.2026) -------------------------------------------------------
// The page runs over plain HTTP, so the NEW password must not cross the WiFi in clear text (SA 7.2 attacker F,
// 8.3). The page asks for the current password too and sends
//   enc = new (zero-padded to 32 bytes) XOR [MD5(cur:nonce:k1) | MD5(cur:nonce:k2)]   (64 hex)
//   mac = MD5(cur:nonce:new)                                                            (32 hex)
// with a one-time nonce from the unit. Someone who records it lacks `cur` (the random label password, or an own
// one) and learns nothing; a wrong current password fails the mac. Empty new password = back to the label.
constexpr size_t kPasswordMin = 4;   // short own passwords are the buyer's choice; the page recommends 10+
constexpr size_t kPasswordMax = 32;  // = CbProvisioning::dev_pass
bool password_allowed(const char *pw);  // length, printable ASCII, no leading/trailing space
// -> true and `out` = the new password ("" = label) if the mac fits and the password is allowed.
bool password_change_open(const char *cur, const char *nonce, const char *enc_hex, const char *mac_hex, char *out,
                          size_t cap, bool *wrong_current);
// The page's side, for the PC tests and tools.
void password_change_seal(const char *cur, const char *nonce, const char *pw, char enc_hex[2 * kPasswordMax + 1],
                          char mac_hex[33]);

// ---- Server side --------------------------------------------------------------------------------------------
enum class AuthResult : uint8_t {
  Ok,
  Missing,    // no Authorization header -> 401 with challenge (normal first request)
  Bad,        // wrong user/password or malformed -> 401, counts towards the lock
  Stale,      // nonce expired, from before a reboot, or not ours -> 401 with stale=true (browser retries
              // silently); not a failure: the password was not checked
  Replay,     // nonce count did not grow -> 401
  WrongUri,   // header was computed for another URI -> 401
  LockedOut,  // too many wrong passwords -> 429, not even checked
};
const char *auth_result_name(AuthResult r);

class DigestAuth {
 public:
  static constexpr uint32_t kNonceLifetimeS = 600;
  static constexpr int kMaxFailures = 5;
  static constexpr uint32_t kLockS = 30;
  static constexpr int kTracked = 8;  // nonces whose count is remembered

  // secret: random per boot (device: hardware RNG). realm is shown by the browser in the login dialog.
  void begin(const uint8_t secret[32], const char *realm, const char *user, const char *password);
  // Own password set on the page: takes effect at once; resets the wrong-password counter.
  void set_password(const char *password);

  // Value for the WWW-Authenticate header. now_s: monotonic seconds (uptime).
  void challenge(uint32_t now_s, bool stale, char *out, size_t n);

  // header: value of the Authorization header or nullptr. method "GET"/"PUT", uri as requested (with query).
  AuthResult check(const char *header, const char *method, const char *uri, uint32_t now_s);

  uint32_t ok_count() const { return ok_; }
  uint32_t fail_count() const { return fail_total_; }
  uint32_t lock_count() const { return locks_; }
  bool locked(uint32_t now_s) const { return lock_until_ && (int32_t)(lock_until_ - now_s) > 0; }

 private:
  bool make_nonce(uint32_t seq, uint32_t ts, char out[41]) const;
  bool nonce_valid(const char *nonce, uint32_t now_s, uint32_t *seq) const;
  AuthResult fail(uint32_t now_s);

  uint8_t secret_[32] = {};
  char realm_[48] = "";
  char user_[33] = "";
  char pass_[65] = "";
  uint32_t next_seq_ = 1;
  uint32_t floor_seq_ = 1;  // nonces below this were evicted from the table -> stale
  struct Tracked {
    uint32_t seq;
    uint32_t nc;
  } tracked_[kTracked] = {};
  int failures_ = 0;
  uint32_t lock_until_ = 0;
  uint32_t ok_ = 0, fail_total_ = 0, locks_ = 0;
};

}  // namespace cb
