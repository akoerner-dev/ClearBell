// cb_remote_logic.cpp - see cb_remote_logic.h.
#include "cb_remote_logic.h"

#include <cstdio>
#include <cstring>

#include "cb_bytes.h"
#include "cb_crypto.h"

namespace cb {

namespace {

constexpr const char *kScheme = "CBR2";

bool is_hex_lower(const char *s, size_t n) {
  for (size_t i = 0; i < n; i++) {
    char c = s[i];
    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
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

bool topic_from(const char *key_hex, const char *label, char out[kTopicLen + 1]) {
  uint8_t mac[kMacLen];
  Slice part = {reinterpret_cast<const uint8_t *>(label), strlen(label)};
  if (!hmac_sha256(reinterpret_cast<const uint8_t *>(key_hex), kPhoneKeyHex, &part, 1, mac)) return false;
  out[0] = 'c';
  out[1] = 'b';
  to_hex(mac, 16, out + 2);
  return true;
}

// "CBR2 <32 hex>" with surrounding whitespace (phones and ntfy may add a newline) -> token, or false.
bool parse(const char *msg, char token[33], bool *scheme_ok) {
  *scheme_ok = false;
  if (!msg) return false;
  while (*msg == ' ' || *msg == '\n' || *msg == '\r' || *msg == '\t') msg++;
  const char *sp = strchr(msg, ' ');
  if (!sp) return false;
  *scheme_ok = (size_t)(sp - msg) == 4 && strncmp(msg, kScheme, 4) == 0;
  const char *t = sp + 1;
  size_t n = 0;
  while (t[n] && t[n] != ' ' && t[n] != '\n' && t[n] != '\r' && t[n] != '\t') n++;
  const char *rest = t + n;
  while (*rest == ' ' || *rest == '\n' || *rest == '\r' || *rest == '\t') rest++;
  if (n != 32 || *rest || !is_hex_lower(t, 32)) return false;
  memcpy(token, t, 32);
  token[32] = 0;
  return true;
}

}  // namespace

const char *remote_result_name(RemoteResult r) {
  switch (r) {
    case RemoteResult::Ok: return "ok";
    case RemoteResult::Malformed: return "malformed";
    case RemoteResult::UnknownScheme: return "unknown scheme";
    case RemoteResult::UnknownPhone: return "unknown phone (topic)";
    case RemoteResult::UnknownToken: return "unknown or used token";
    case RemoteResult::Expired: return "token expired";
  }
  return "?";
}

bool valid_phone_id(const char *s) {
  if (!s) return false;
  size_t n = strlen(s);
  if (n == 0 || n > kPhoneIdMax) return false;
  for (size_t i = 0; i < n; i++) {
    char c = s[i];
    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '-')) return false;
  }
  return true;
}

bool phone_topics(const char *key_hex, char push[kTopicLen + 1], char cmd[kTopicLen + 1]) {
  if (!key_hex || strlen(key_hex) != kPhoneKeyHex || !is_hex_lower(key_hex, kPhoneKeyHex)) return false;
  return topic_from(key_hex, "CBT1|push", push) && topic_from(key_hex, "CBT1|cmd", cmd);
}

bool RemoteAuth::set_phone(int slot, const char *id, const char *key_hex, uint8_t flat) {
  if (slot < 0 || slot >= kRemotePhones || (flat != 2 && flat != 3) || !valid_phone_id(id)) return false;
  Phone p{};
  if (!phone_topics(key_hex, p.push, p.cmd)) return false;
  snprintf(p.id, sizeof p.id, "%s", id);
  snprintf(p.key, sizeof p.key, "%s", key_hex);
  p.flat = flat;
  p.used = true;
  phones_[slot] = p;
  for (Token &t : tokens_)
    if (t.used && t.slot == slot) t = Token{};  // a replaced phone does not inherit open tokens
  return true;
}

int RemoteAuth::add_phone(const char *id, const char *key_hex, uint8_t flat) {
  int slot = -1;
  for (int i = 0; i < kRemotePhones; i++)
    if (phones_[i].used && phones_[i].flat == flat && id && strcmp(phones_[i].id, id) == 0) slot = i;
  if (slot < 0 && phone_count(flat) >= kPhonesPerFlat) return -1;
  for (int i = 0; i < kRemotePhones && slot < 0; i++)
    if (!phones_[i].used) slot = i;
  return slot >= 0 && set_phone(slot, id, key_hex, flat) ? slot : -1;
}

bool RemoteAuth::remove_phone(const char *id, uint8_t flat) {
  for (int i = 0; i < kRemotePhones; i++)
    if (phones_[i].used && phones_[i].flat == flat && id && strcmp(phones_[i].id, id) == 0) {
      phones_[i] = Phone{};
      for (Token &t : tokens_)
        if (t.used && t.slot == i) t = Token{};
      return true;
    }
  return false;
}

int RemoteAuth::remove_flat(uint8_t flat) {
  int n = 0;
  for (int i = 0; i < kRemotePhones; i++) {
    if (!phones_[i].used || phones_[i].flat != flat) continue;
    phones_[i] = Phone{};
    for (Token &t : tokens_)
      if (t.used && t.slot == i) t = Token{};
    n++;
  }
  return n;
}

int RemoteAuth::phone_count() const {
  int n = 0;
  for (const Phone &p : phones_) n += p.used;
  return n;
}

int RemoteAuth::phone_count(uint8_t flat) const {
  int n = 0;
  for (const Phone &p : phones_) n += p.used && p.flat == flat;
  return n;
}

bool RemoteAuth::cmd_topics(char *out, size_t cap) const {
  size_t n = 0;
  out[0] = 0;
  for (const Phone &p : phones_) {
    if (!p.used) continue;
    int w = snprintf(out + n, cap - n, "%s%s", n ? "," : "", p.cmd);
    if (w < 0 || (size_t)w >= cap - n) return false;
    n += (size_t)w;
  }
  return true;
}

int RemoteAuth::issue(uint8_t flat, uint32_t now_ms, int slots[kPhonesPerFlat], char tokens[kPhonesPerFlat][33]) {
  // Keep at most kRemoteRings rings per flat: drop the tokens of the oldest one(s).
  for (;;) {
    uint32_t oldest = 0;
    int distinct = 0;
    for (int i = 0; i < kRemoteTokens * 2; i++) {
      const Token &t = tokens_[i];
      if (!t.used || phones_[t.slot].flat != flat) continue;
      bool seen = false;
      for (int k = 0; k < i; k++)
        seen |= tokens_[k].used && tokens_[k].ring == t.ring && phones_[tokens_[k].slot].flat == flat;
      if (!seen) {
        distinct++;
        if (!oldest || t.ring < oldest) oldest = t.ring;
      }
    }
    if (distinct < kRemoteRings) break;
    for (Token &t : tokens_)
      if (t.used && t.ring == oldest && phones_[t.slot].flat == flat) t = Token{};
  }
  uint32_t ring = ++rings_;
  int n = 0;
  for (int s = 0; s < kRemotePhones && n < kPhonesPerFlat; s++) {
    if (!phones_[s].used || phones_[s].flat != flat) continue;
    Token *free_t = nullptr;
    for (Token &t : tokens_)
      if (!t.used) {
        free_t = &t;
        break;
      }
    if (!free_t) break;  // cannot happen: 3 rings x 4 phones x 2 flats = array size
    uint8_t raw[16];
    random_bytes(raw, sizeof raw);
    to_hex(raw, sizeof raw, free_t->hex);
    free_t->issued = now_ms;
    free_t->ring = ring;
    free_t->slot = (int8_t)s;
    free_t->used = true;
    slots[n] = s;
    memcpy(tokens[n], free_t->hex, 33);
    n++;
  }
  return n;
}

RemoteResult RemoteAuth::check(const char *topic, const char *msg, uint32_t now_ms, int *slot_out) const {
  *slot_out = -1;
  char token[33];
  bool scheme_ok = false;
  bool parsed = parse(msg, token, &scheme_ok);
  if (!scheme_ok) return msg && strchr(msg, ' ') && parsed ? RemoteResult::UnknownScheme : RemoteResult::Malformed;
  if (!parsed) return RemoteResult::Malformed;
  int slot = -1;
  for (int i = 0; i < kRemotePhones; i++)
    if (phones_[i].used && topic && strcmp(phones_[i].cmd, topic) == 0) slot = i;
  if (slot < 0) return RemoteResult::UnknownPhone;
  const Token *tok = nullptr;
  for (const Token &t : tokens_)
    if (t.used && t.slot == slot &&
        ct_equal(reinterpret_cast<const uint8_t *>(t.hex), reinterpret_cast<const uint8_t *>(token), 32))
      tok = &t;
  if (!tok) return RemoteResult::UnknownToken;
  if (now_ms - tok->issued > kRemoteTokenMs) return RemoteResult::Expired;
  *slot_out = slot;
  return RemoteResult::Ok;
}

void RemoteAuth::consume_flat(uint8_t flat) {
  for (Token &t : tokens_)
    if (t.used && phones_[t.slot].flat == flat) t = Token{};
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
