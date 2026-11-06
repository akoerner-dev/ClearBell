// cb_remote_logic.cpp - see cb_remote_logic.h.
#include "cb_remote_logic.h"

#include <cstdio>
#include <cstring>

#include "cb_bytes.h"
#include "cb_crypto.h"

namespace cb {

namespace {

constexpr const char *kScheme = "CBR1";

bool is_hex_lower(const char *s, size_t n) {
  for (size_t i = 0; i < n; i++) {
    char c = s[i];
    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
  }
  return true;
}

bool valid_id(const char *s) {
  size_t n = strlen(s);
  if (n == 0 || n > kPhoneIdMax) return false;
  for (size_t i = 0; i < n; i++) {
    char c = s[i];
    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '-')) return false;
  }
  return true;
}

void to_hex(const uint8_t *in, size_t n, char *out) {
  static const char h[] = "0123456789abcdef";
  for (size_t i = 0; i < n; i++) {
    out[2 * i] = h[in[i] >> 4];
    out[2 * i + 1] = h[in[i] & 15];
  }
  out[2 * n] = 0;
}

// Splits "a b c d" into exactly four words (single spaces, surrounding whitespace trimmed).
bool split4(const char *msg, char w[4][80]) {
  while (*msg == ' ' || *msg == '\n' || *msg == '\r' || *msg == '\t') msg++;
  for (int i = 0; i < 4; i++) {
    size_t n = 0;
    while (*msg && *msg != ' ' && *msg != '\n' && *msg != '\r' && *msg != '\t') {
      if (n + 1 >= 80) return false;
      w[i][n++] = *msg++;
    }
    w[i][n] = 0;
    if (n == 0) return false;
    if (i < 3) {
      if (*msg != ' ') return false;
      msg++;
    }
  }
  while (*msg == ' ' || *msg == '\n' || *msg == '\r' || *msg == '\t') msg++;
  return *msg == 0;
}

}  // namespace

const char *remote_result_name(RemoteResult r) {
  switch (r) {
    case RemoteResult::Ok: return "ok";
    case RemoteResult::Malformed: return "malformed";
    case RemoteResult::UnknownScheme: return "unknown scheme";
    case RemoteResult::UnknownPhone: return "unknown phone";
    case RemoteResult::UnknownToken: return "unknown or used token";
    case RemoteResult::Expired: return "token expired";
    case RemoteResult::BadSignature: return "BAD SIGNATURE";
  }
  return "?";
}

bool RemoteAuth::set_phone(int slot, const char *id, const char *key_hex) {
  if (slot < 0 || slot >= kRemotePhones) return false;
  if (!id || !key_hex || !valid_id(id) || strlen(key_hex) != kPhoneKeyHex || !is_hex_lower(key_hex, kPhoneKeyHex))
    return false;
  Phone &p = phones_[slot];
  snprintf(p.id, sizeof p.id, "%s", id);
  snprintf(p.key, sizeof p.key, "%s", key_hex);
  p.used = true;
  return true;
}

int RemoteAuth::add_phone(const char *id, const char *key_hex) {
  int slot = -1;
  for (int i = 0; i < kRemotePhones; i++)
    if (phones_[i].used && id && strcmp(phones_[i].id, id) == 0) slot = i;  // same id: replace (new phone)
  for (int i = 0; i < kRemotePhones && slot < 0; i++)
    if (!phones_[i].used) slot = i;
  return slot >= 0 && set_phone(slot, id, key_hex) ? slot : -1;
}

bool RemoteAuth::remove_phone(const char *id) {
  for (Phone &p : phones_)
    if (p.used && id && strcmp(p.id, id) == 0) {
      p = Phone{};
      return true;
    }
  return false;
}

int RemoteAuth::phone_count() const {
  int n = 0;
  for (const Phone &p : phones_) n += p.used;
  return n;
}

void RemoteAuth::issue(uint32_t now_ms, char token_out[33]) {
  uint8_t raw[16];
  random_bytes(raw, sizeof raw);
  Token &t = tokens_[next_];
  to_hex(raw, sizeof raw, t.hex);
  t.issued = now_ms;
  t.used = true;
  next_ = (next_ + 1) % kRemoteTokens;
  memcpy(token_out, t.hex, 33);
}

bool RemoteAuth::sign(const char *key_hex, const char *token, const char *phone, char out[65]) {
  uint8_t mac[kMacLen];
  Slice parts[] = {{reinterpret_cast<const uint8_t *>(kScheme), 4},
                   {reinterpret_cast<const uint8_t *>("|"), 1},
                   {reinterpret_cast<const uint8_t *>(token), strlen(token)},
                   {reinterpret_cast<const uint8_t *>("|"), 1},
                   {reinterpret_cast<const uint8_t *>(phone), strlen(phone)}};
  if (!hmac_sha256(reinterpret_cast<const uint8_t *>(key_hex), strlen(key_hex), parts, 5, mac)) return false;
  to_hex(mac, sizeof mac, out);
  return true;
}

RemoteResult RemoteAuth::check(const char *msg, uint32_t now_ms, char phone_out[kPhoneIdMax + 1]) const {
  phone_out[0] = 0;
  char w[4][80];
  if (!msg || !split4(msg, w)) return RemoteResult::Malformed;
  if (strcmp(w[0], kScheme) != 0) return RemoteResult::UnknownScheme;
  if (strlen(w[1]) != 32 || !is_hex_lower(w[1], 32) || !valid_id(w[2])) return RemoteResult::Malformed;
  // Phone tools may print the signature in upper case: normalise.
  if (strlen(w[3]) != 64) return RemoteResult::Malformed;
  for (char *c = w[3]; *c; c++)
    if (*c >= 'A' && *c <= 'F') *c = (char)(*c - 'A' + 'a');
  if (!is_hex_lower(w[3], 64)) return RemoteResult::Malformed;

  const Phone *phone = nullptr;
  for (const Phone &p : phones_)
    if (p.used && strcmp(p.id, w[2]) == 0) phone = &p;
  if (!phone) return RemoteResult::UnknownPhone;

  const Token *tok = nullptr;
  for (const Token &t : tokens_)
    if (t.used && ct_equal(reinterpret_cast<const uint8_t *>(t.hex), reinterpret_cast<const uint8_t *>(w[1]), 32))
      tok = &t;
  if (!tok) return RemoteResult::UnknownToken;
  if (now_ms - tok->issued > kRemoteTokenMs) return RemoteResult::Expired;

  char expect[65];
  if (!sign(phone->key, w[1], w[2], expect)) return RemoteResult::BadSignature;  // fail closed
  if (!ct_equal(reinterpret_cast<const uint8_t *>(expect), reinterpret_cast<const uint8_t *>(w[3]), 64))
    return RemoteResult::BadSignature;
  snprintf(phone_out, kPhoneIdMax + 1, "%s", phone->id);
  return RemoteResult::Ok;
}

void RemoteAuth::consume_all() {
  for (Token &t : tokens_) t = Token{};
}

int RemoteAuth::active(uint32_t now_ms) const {
  int n = 0;
  for (const Token &t : tokens_) n += t.used && now_ms - t.issued <= kRemoteTokenMs;
  return n;
}

// ---- JSON: walk the top-level object key by key -------------------------------------------------------------

namespace {

const char *skip_ws(const char *p) {
  while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
  return p;
}

// Parses a JSON string at p (pointing at the opening quote). Writes up to cap-1 chars to out (if out != null).
// Returns the position after the closing quote, or nullptr on error / overflow (when out != null).
const char *parse_string(const char *p, char *out, size_t cap, bool *overflow) {
  if (*p != '"') return nullptr;
  p++;
  size_t n = 0;
  while (*p && *p != '"') {
    char c = *p++;
    if (c == '\\') {
      char e = *p++;
      switch (e) {
        case '"': c = '"'; break;
        case '\\': c = '\\'; break;
        case '/': c = '/'; break;
        case 'n': c = '\n'; break;
        case 't': c = '\t'; break;
        case 'r': c = '\r'; break;
        case 'b': c = '\b'; break;
        case 'f': c = '\f'; break;
        case 'u':
          for (int i = 0; i < 4; i++)
            if (!*p++) return nullptr;
          c = '?';  // never needed for our fields; keep the length right
          break;
        default: return nullptr;
      }
    } else if ((unsigned char)c < 0x20) {
      return nullptr;
    }
    if (out) {
      if (n + 1 >= cap) {
        *overflow = true;
        return nullptr;
      }
      out[n++] = c;
    }
  }
  if (*p != '"') return nullptr;
  if (out) out[n] = 0;
  return p + 1;
}

// Skips any JSON value; returns the position after it or nullptr.
const char *skip_value(const char *p, int depth) {
  p = skip_ws(p);
  if (depth > 8) return nullptr;
  if (*p == '"') {
    bool of = false;
    return parse_string(p, nullptr, 0, &of);
  }
  if (*p == '{' || *p == '[') {
    char close = *p == '{' ? '}' : ']';
    bool obj = *p == '{';
    p = skip_ws(p + 1);
    if (*p == close) return p + 1;
    for (;;) {
      if (obj) {
        bool of = false;
        p = parse_string(skip_ws(p), nullptr, 0, &of);
        if (!p) return nullptr;
        p = skip_ws(p);
        if (*p++ != ':') return nullptr;
      }
      p = skip_value(p, depth + 1);
      if (!p) return nullptr;
      p = skip_ws(p);
      if (*p == ',') {
        p++;
        continue;
      }
      if (*p == close) return p + 1;
      return nullptr;
    }
  }
  // number, true, false, null
  const char *start = p;
  while (*p && *p != ',' && *p != '}' && *p != ']' && *p != ' ' && *p != '\n' && *p != '\r' && *p != '\t') p++;
  return p > start ? p : nullptr;
}

}  // namespace

bool json_get_string(const char *json, const char *key, char *out, size_t cap) {
  if (!json || !key || !out || cap == 0) return false;
  out[0] = 0;
  const char *p = skip_ws(json);
  if (*p != '{') return false;
  p = skip_ws(p + 1);
  if (*p == '}') return false;
  char k[32];
  for (;;) {
    bool of = false;
    const char *after = parse_string(p, k, sizeof k, &of);
    bool match = after && strcmp(k, key) == 0;
    if (!after) {
      if (!of) return false;
      // key longer than our buffer: cannot be the one we look for - skip it
      after = parse_string(p, nullptr, 0, &of);
      if (!after) return false;
    }
    p = skip_ws(after);
    if (*p++ != ':') return false;
    p = skip_ws(p);
    if (match) {
      if (*p != '"') return false;
      of = false;
      return parse_string(p, out, cap, &of) != nullptr;
    }
    p = skip_value(p, 1);
    if (!p) return false;
    p = skip_ws(p);
    if (*p == ',') {
      p = skip_ws(p + 1);
      continue;
    }
    return false;  // '}' (key not found) or garbage
  }
}

}  // namespace cb
