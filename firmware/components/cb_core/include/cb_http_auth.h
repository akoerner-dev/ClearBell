// cb_http_auth.h - device password for the status page: HTTP Digest authentication (RFC 7616, MD5).
// Hardware-free, PC-tested (test/host/test_status.cpp).
//
// Why Digest and not Basic: the status page runs over plain HTTP in the house WiFi (Softwarearchitektur
// 8.3). With Basic the password would cross the WiFi in clear text, readable by every device that knows the
// WiFi key - i.e. also by the other household (attacker F, SA 7.2). With Digest only a hash bound to a
// one-time server nonce crosses the air. MD5 is the one algorithm every browser speaks. Its weakness is harmless
// for the label code (device code, 16 characters of Crockford Base32 = 80 bit, tools/provisionieren.py): guessing
// it from a recorded exchange is infeasible. With an OWN password the security of Digest-MD5 depends on that
// password's length and choice: a recorded exchange can be tried offline against a dictionary. Hence at least
// 10 characters for newly set passwords, and the page advises a long, unique one.
//
// Protection built in:
//   - nonces are stateless (time + sequence number + HMAC with a secret drawn at boot), valid 10 min;
//     a reboot invalidates all of them ("stale" -> the browser asks again silently, no password prompt)
//   - nonce count must grow per nonce -> a recorded request cannot be replayed (matters for uploads)
//   - the `uri` in the header must be the requested URI -> an answer for one page does not open another
//   - wrong passwords are counted PER SOURCE ADDRESS (RV-15): 5 in a row lock that source for 30 s, then 60 s,
//     120 s ... at most 15 min on repetition; a success of that source resets it. Other sources are not affected,
//     so one device in the WiFi cannot lock the residents out. A malformed header or a wrong realm does not
//     count (the password was not checked). Against many/forged source addresses a global brake limits
//     password checks to one per second for everybody once 50 wrong passwords came in within 10 min.
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
constexpr size_t kPasswordMin = 10;  // RV-14: only for NEWLY set passwords; a stored shorter one stays valid at boot
constexpr size_t kPasswordMax = 32;  // = CbProvisioning::dev_pass
bool password_allowed(const char *pw);  // length, printable ASCII, no leading/trailing space

// The ways people copy the 16-character device code from a label (FW-B-14): as is, in groups of four with '-' or
// ' ', and in lower case. Returns the number of variants written to `out` (the canonical form first), or 0 if
// `pass` is not a device code (an own password gets no variants).
constexpr int kCodeVariantsMax = 4;
constexpr size_t kCodeVariantLen = 24;
int device_code_variants(const char *pass, char out[kCodeVariantsMax][kCodeVariantLen]);
// -> true and `out` = the new password ("" = label) if the mac fits and the password is allowed.
bool password_change_open(const char *cur, const char *nonce, const char *enc_hex, const char *mac_hex, char *out,
                          size_t cap, bool *wrong_current);
// RV-43: like password_change_open, but `stored` is the password the unit holds: while it is the device code the
// current password may be typed in any label form (device_code_variants), every candidate is tried. `wrong_current`
// is true only if ALL candidates failed (one failed attempt per request, not one per candidate).
bool password_change_open_stored(const char *stored, const char *nonce, const char *enc_hex, const char *mac_hex,
                                 char *out, size_t cap, bool *wrong_current);
// The page's side, for the PC tests and tools.
void password_change_seal(const char *cur, const char *nonce, const char *pw, char enc_hex[2 * kPasswordMax + 1],
                          char mac_hex[33]);

// ---- Server side --------------------------------------------------------------------------------------------
enum class AuthResult : uint8_t {
  Ok,
  Missing,    // no Authorization header -> 401 with challenge (normal first request)
  Bad,        // wrong user/password -> 401, counts towards the lock of this source
  Stale,      // nonce expired, from before a reboot, or not ours -> 401 with stale=true (browser retries
              // silently); not a failure: the password was not checked
  Replay,     // nonce count did not grow -> 401
  WrongUri,   // header was computed for another URI -> 401
  LockedOut,  // this source has too many wrong passwords, or the global brake is on -> 429, not even checked
  Malformed,  // header not parseable or wrong realm -> new 401 challenge; not counted: password not checked
};
const char *auth_result_name(AuthResult r);

class DigestAuth {
 public:
  static constexpr uint32_t kNonceLifetimeS = 600;
  static constexpr int kMaxFailures = 5;
  static constexpr uint32_t kLockS = 30;       // first lock of a source; doubles on every repetition
  static constexpr uint32_t kLockMaxS = 900;   // ... up to 15 min
  static constexpr int kTracked = 8;  // nonces whose count is remembered
  static constexpr int kSources = 8;  // source addresses whose wrong passwords are remembered
  static constexpr int kGlobalMaxFails = 50;           // wrong passwords of all sources together ...
  static constexpr uint32_t kGlobalWindowS = 600;      // ... within this window switch the brake on
  // brake on: at most one password check per second for everybody (throttled, not locked)

  // secret: random per boot (device: hardware RNG). realm is shown by the browser in the login dialog.
  void begin(const uint8_t secret[32], const char *realm, const char *user, const char *password);
  // Own password set on the page: takes effect at once; resets the wrong-password counters (running locks stay).
  void set_password(const char *password);

  // Value for the WWW-Authenticate header. now_s: monotonic seconds (uptime).
  void challenge(uint32_t now_s, bool stale, char *out, size_t n);

  // header: value of the Authorization header or nullptr. method "GET"/"PUT", uri as requested (with query).
  // source: IPv4 address of the sender (any stable 32-bit value; 0 = unknown, counted as one source).
  AuthResult check(const char *header, const char *method, const char *uri, uint32_t now_s, uint32_t source);

  uint32_t ok_count() const { return ok_; }
  uint32_t fail_count() const { return fail_total_; }
  uint32_t lock_count() const { return locks_; }
  bool locked(uint32_t now_s, uint32_t source) const;
  // Seconds until `source` may try again (0 = may try); 1 while only the global brake holds it back.
  uint32_t retry_after_s(uint32_t now_s, uint32_t source) const;
  bool braking(uint32_t now_s) const;  // global brake on

 private:
  bool make_nonce(uint32_t seq, uint32_t ts, char out[41]) const;
  bool nonce_valid(const char *nonce, uint32_t now_s, uint32_t *seq) const;
  AuthResult fail(uint32_t now_s, uint32_t source);

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
  struct Source {
    bool used;
    uint32_t addr;
    int fails;          // wrong passwords since the last lock / success
    uint8_t level;      // locks so far -> next lock lasts kLockS << level
    uint32_t locked_until;  // 0 = not locked
    uint32_t last;      // time of the last wrong password (age for replacement)
  } src_[kSources] = {};
  Source *find_source(uint32_t addr);
  const Source *find_source(uint32_t addr) const;
  bool source_locked(const Source *s, uint32_t now_s) const {
    return s && s->locked_until && (int32_t)(s->locked_until - now_s) > 0;
  }
  int global_fails_ = 0;
  uint32_t global_since_ = 0;
  uint32_t last_check_s_ = 0;
  bool checked_once_ = false;
  uint32_t ok_ = 0, fail_total_ = 0, locks_ = 0;
};

}  // namespace cb
