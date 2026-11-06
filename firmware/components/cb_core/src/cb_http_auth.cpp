// cb_http_auth.cpp - see cb_http_auth.h.
#include "cb_http_auth.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "cb_bytes.h"
#include "cb_crypto.h"

namespace cb {

// ---- MD5, straight from RFC 1321 ---------------------------------------------------------------------------
namespace {

constexpr uint32_t kT[64] = {
    0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501,
    0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be, 0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821,
    0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa, 0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
    0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a,
    0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c, 0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70,
    0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05, 0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
    0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1,
    0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1, 0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391};
constexpr uint8_t kS[64] = {7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 5, 9,  14, 20, 5, 9,
                            14, 20, 5, 9,  14, 20, 5, 9,  14, 20, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
                            4, 11, 16, 23, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21};

inline uint32_t rotl(uint32_t x, int n) { return (x << n) | (x >> (32 - n)); }

void md5_block(Md5 *m, const uint8_t *p) {
  uint32_t x[16];
  for (int i = 0; i < 16; i++) x[i] = get_u32le(p + 4 * i);
  uint32_t a = m->a, b = m->b, c = m->c, d = m->d;
  for (int i = 0; i < 64; i++) {
    uint32_t f;
    int g;
    if (i < 16) {
      f = (b & c) | (~b & d);
      g = i;
    } else if (i < 32) {
      f = (d & b) | (~d & c);
      g = (5 * i + 1) % 16;
    } else if (i < 48) {
      f = b ^ c ^ d;
      g = (3 * i + 5) % 16;
    } else {
      f = c ^ (b | ~d);
      g = (7 * i) % 16;
    }
    uint32_t t = d;
    d = c;
    c = b;
    b = b + rotl(a + f + kT[i] + x[g], kS[i]);
    a = t;
  }
  m->a += a;
  m->b += b;
  m->c += c;
  m->d += d;
}

void to_hex(const uint8_t *in, size_t n, char *out) {
  static const char h[] = "0123456789abcdef";
  for (size_t i = 0; i < n; i++) {
    out[2 * i] = h[in[i] >> 4];
    out[2 * i + 1] = h[in[i] & 15];
  }
  out[2 * n] = 0;
}

// MD5 over "p1:p2:...:pn" as lowercase hex (the shape of every Digest hash).
void md5_joined_hex(const char *const *parts, int n, char out[33]) {
  Md5 m;
  md5_init(&m);
  for (int i = 0; i < n; i++) {
    if (i) md5_update(&m, ":", 1);
    md5_update(&m, parts[i], strlen(parts[i]));
  }
  uint8_t d[16];
  md5_final(&m, d);
  to_hex(d, 16, out);
}

}  // namespace

void md5_init(Md5 *m) {
  m->a = 0x67452301;
  m->b = 0xefcdab89;
  m->c = 0x98badcfe;
  m->d = 0x10325476;
  m->block_len = 0;
  m->total = 0;
}

void md5_update(Md5 *m, const void *data, size_t len) {
  const uint8_t *p = static_cast<const uint8_t *>(data);
  m->total += len;
  while (len) {
    size_t k = 64 - m->block_len;
    if (k > len) k = len;
    memcpy(m->block + m->block_len, p, k);
    m->block_len += k;
    p += k;
    len -= k;
    if (m->block_len == 64) {
      md5_block(m, m->block);
      m->block_len = 0;
    }
  }
}

void md5_final(Md5 *m, uint8_t out[16]) {
  uint64_t bits = m->total * 8;
  uint8_t pad = 0x80;
  md5_update(m, &pad, 1);
  uint8_t zero = 0;
  while (m->block_len != 56) md5_update(m, &zero, 1);
  uint8_t len[8];
  for (int i = 0; i < 8; i++) len[i] = (uint8_t)(bits >> (8 * i));
  md5_update(m, len, 8);
  put_u32le(out, m->a);
  put_u32le(out + 4, m->b);
  put_u32le(out + 8, m->c);
  put_u32le(out + 12, m->d);
}

void md5_hex(const char *text, char out[33]) {
  md5_joined_hex(&text, 1, out);
}

// ---- Header parsing ----------------------------------------------------------------------------------------

namespace {

bool copy_field(char *dst, size_t cap, const char *src, size_t len) {
  if (len >= cap) return false;
  memcpy(dst, src, len);
  dst[len] = 0;
  return true;
}

bool name_is(const char *name, size_t len, const char *want) {
  if (strlen(want) != len) return false;
  for (size_t i = 0; i < len; i++) {
    char c = name[i];
    if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
    if (c != want[i]) return false;
  }
  return true;
}

}  // namespace

bool parse_digest(const char *h, DigestFields *f) {
  memset(f, 0, sizeof(*f));
  if (!h) return false;
  while (*h == ' ') h++;
  if (strncmp(h, "Digest ", 7) != 0) return false;
  h += 7;
  while (*h) {
    while (*h == ' ' || *h == ',') h++;
    if (!*h) break;
    const char *name = h;
    while (*h && *h != '=' && *h != ' ' && *h != ',') h++;
    size_t name_len = (size_t)(h - name);
    while (*h == ' ') h++;
    if (*h != '=') return false;
    h++;
    while (*h == ' ') h++;
    const char *val;
    size_t val_len;
    if (*h == '"') {
      val = ++h;
      while (*h && *h != '"') {
        if (*h == '\\') return false;  // escapes never occur in our fields; refuse instead of guessing
        h++;
      }
      if (*h != '"') return false;
      val_len = (size_t)(h - val);
      h++;
    } else {
      val = h;
      while (*h && *h != ',' && *h != ' ') h++;
      val_len = (size_t)(h - val);
    }
    struct {
      const char *key;
      char *dst;
      size_t cap;
    } map[] = {{"username", f->username, sizeof(f->username)}, {"realm", f->realm, sizeof(f->realm)},
               {"nonce", f->nonce, sizeof(f->nonce)},          {"uri", f->uri, sizeof(f->uri)},
               {"response", f->response, sizeof(f->response)}, {"qop", f->qop, sizeof(f->qop)},
               {"nc", f->nc, sizeof(f->nc)},                   {"cnonce", f->cnonce, sizeof(f->cnonce)},
               {"algorithm", f->algorithm, sizeof(f->algorithm)}};
    for (auto &m : map) {
      if (name_is(name, name_len, m.key)) {
        if (!copy_field(m.dst, m.cap, val, val_len)) return false;
        break;
      }
    }
  }
  // qop=auth is mandatory: without it there is no client nonce and no nonce count (RFC 2069 mode).
  return f->username[0] && f->nonce[0] && f->uri[0] && f->response[0] && f->cnonce[0] && f->nc[0] &&
         strcmp(f->qop, "auth") == 0 && (!f->algorithm[0] || strcmp(f->algorithm, "MD5") == 0);
}

void digest_response(const DigestFields &f, const char *password, const char *method, char out[33]) {
  char ha1[33], ha2[33];
  const char *p1[] = {f.username, f.realm, password};
  md5_joined_hex(p1, 3, ha1);
  const char *p2[] = {method, f.uri};
  md5_joined_hex(p2, 2, ha2);
  const char *p3[] = {ha1, f.nonce, f.nc, f.cnonce, f.qop, ha2};
  md5_joined_hex(p3, 6, out);
}

// ---- Server side -------------------------------------------------------------------------------------------

const char *auth_result_name(AuthResult r) {
  switch (r) {
    case AuthResult::Ok: return "ok";
    case AuthResult::Missing: return "missing";
    case AuthResult::Bad: return "bad";
    case AuthResult::Stale: return "stale";
    case AuthResult::Replay: return "replay";
    case AuthResult::WrongUri: return "wrong-uri";
    case AuthResult::LockedOut: return "locked-out";
  }
  return "?";
}

void DigestAuth::begin(const uint8_t secret[32], const char *realm, const char *user, const char *password) {
  memcpy(secret_, secret, sizeof(secret_));
  snprintf(realm_, sizeof(realm_), "%s", realm);
  snprintf(user_, sizeof(user_), "%s", user);
  snprintf(pass_, sizeof(pass_), "%s", password);
}

// nonce = 8 hex sequence number | 8 hex issue time | 24 hex HMAC(secret, "cbnonce" seq time)[0..11]
bool DigestAuth::make_nonce(uint32_t seq, uint32_t ts, char out[41]) const {
  uint8_t msg[8], mac[kMacLen];
  put_u32le(msg, seq);
  put_u32le(msg + 4, ts);
  Slice parts[] = {{reinterpret_cast<const uint8_t *>("cbnonce"), 7}, {msg, sizeof(msg)}};
  if (!hmac_sha256(secret_, sizeof(secret_), parts, 2, mac)) return false;
  snprintf(out, 41, "%08lx%08lx", (unsigned long)seq, (unsigned long)ts);
  to_hex(mac, 12, out + 16);
  return true;
}

bool DigestAuth::nonce_valid(const char *nonce, uint32_t now_s, uint32_t *seq) const {
  if (strlen(nonce) != 40) return false;
  char buf[9];
  memcpy(buf, nonce, 8);
  buf[8] = 0;
  unsigned long s = strtoul(buf, nullptr, 16);
  memcpy(buf, nonce + 8, 8);
  unsigned long t = strtoul(buf, nullptr, 16);
  char expect[41];
  if (!make_nonce((uint32_t)s, (uint32_t)t, expect)) return false;  // fail closed
  if (!ct_equal(reinterpret_cast<const uint8_t *>(expect), reinterpret_cast<const uint8_t *>(nonce), 40))
    return false;
  if (now_s - (uint32_t)t > kNonceLifetimeS) return false;
  *seq = (uint32_t)s;
  return true;
}

void DigestAuth::challenge(uint32_t now_s, bool stale, char *out, size_t n) {
  char nonce[41];
  if (!make_nonce(next_seq_++, now_s, nonce)) nonce[0] = 0;  // empty nonce: nobody can log in (fail closed)
  snprintf(out, n, "Digest realm=\"%s\", qop=\"auth\", algorithm=MD5, nonce=\"%s\"%s", realm_, nonce,
           stale ? ", stale=true" : "");
}

AuthResult DigestAuth::fail(uint32_t now_s) {
  fail_total_++;
  if (++failures_ >= kMaxFailures) {
    failures_ = 0;
    lock_until_ = now_s + kLockS;
    if (!lock_until_) lock_until_ = 1;
    locks_++;
  }
  return AuthResult::Bad;
}

AuthResult DigestAuth::check(const char *header, const char *method, const char *uri, uint32_t now_s) {
  if (locked(now_s)) return AuthResult::LockedOut;
  if (!header || !*header) return AuthResult::Missing;
  DigestFields f;
  if (!parse_digest(header, &f)) return fail(now_s);
  if (strcmp(f.realm, realm_) != 0) return fail(now_s);

  // Expired nonce, nonce from before a reboot (other secret -> MAC does not match) or a made-up one: "stale",
  // the browser fetches a fresh challenge silently. Not counted as failure - the password was not even
  // checked, so nothing can be guessed this way.
  uint32_t seq = 0;
  if (!nonce_valid(f.nonce, now_s, &seq)) return AuthResult::Stale;
  if (seq < floor_seq_) return AuthResult::Stale;  // its count was forgotten -> must not be trusted again

  char expect[33];
  digest_response(f, pass_, method, expect);
  bool user_ok = strcmp(f.username, user_) == 0;
  bool resp_ok = strlen(f.response) == 32 &&
                 ct_equal(reinterpret_cast<const uint8_t *>(expect), reinterpret_cast<const uint8_t *>(f.response), 32);
  if (!user_ok || !resp_ok) return fail(now_s);
  // Past this point the sender knows the password. Remaining checks protect against replay / misuse of a
  // recorded header and do not count as wrong password.
  if (strcmp(f.uri, uri) != 0) return AuthResult::WrongUri;

  char *end = nullptr;
  unsigned long nc = strtoul(f.nc, &end, 16);
  if (!end || *end || nc == 0) return fail(now_s);
  int slot = -1, oldest = 0;
  for (int i = 0; i < kTracked; i++) {
    if (tracked_[i].seq == seq) slot = i;
    if (tracked_[i].seq < tracked_[oldest].seq) oldest = i;
  }
  if (slot >= 0) {
    if (nc <= tracked_[slot].nc) return AuthResult::Replay;
  } else {
    // First use of this nonce: take the slot of the oldest tracked nonce; everything up to that one is
    // forgotten from now on and therefore stale.
    slot = oldest;
    if (tracked_[slot].seq && tracked_[slot].seq + 1 > floor_seq_) floor_seq_ = tracked_[slot].seq + 1;
    tracked_[slot].seq = seq;
  }
  tracked_[slot].nc = (uint32_t)nc;
  failures_ = 0;
  ok_++;
  return AuthResult::Ok;
}

}  // namespace cb
