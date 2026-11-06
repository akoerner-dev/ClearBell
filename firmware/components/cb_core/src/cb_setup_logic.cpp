// cb_setup_logic.cpp - see cb_setup_logic.h.
#include "cb_setup_logic.h"

#include <cstring>

namespace cb {

namespace {
constexpr char kAlphabet[] = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";  // Crockford Base32

int hexval(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}
}  // namespace

bool device_code_normalize(const char *in, char out[kDeviceCodeLen + 1]) {
  if (!in) return false;
  size_t n = 0;
  for (; *in; in++) {
    char c = *in;
    if (c == '-' || c == ' ') continue;
    if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
    if (c == 'O') c = '0';
    if (c == 'I' || c == 'L') c = '1';
    if (!strchr(kAlphabet, c) || c == 0) return false;  // also refuses U
    if (n >= kDeviceCodeLen) return false;
    out[n++] = c;
  }
  out[n] = 0;
  return n == kDeviceCodeLen;
}

void device_code_group(const char *code, char out[20]) {
  size_t o = 0;
  for (size_t i = 0; i < kDeviceCodeLen && code[i]; i++) {
    if (i && i % 4 == 0) out[o++] = '-';
    out[o++] = code[i];
  }
  out[o] = 0;
}

void device_code_from_bytes(const uint8_t rnd[10], char out[kDeviceCodeLen + 1]) {
  // 80 bit -> 16 x 5 bit, most significant first
  uint32_t acc = 0;
  int bits = 0;
  size_t n = 0;
  for (int i = 0; i < 10; i++) {
    acc = (acc << 8) | rnd[i];
    bits += 8;
    while (bits >= 5) {
      out[n++] = kAlphabet[(acc >> (bits - 5)) & 31];
      bits -= 5;
    }
  }
  out[n] = 0;
}

bool form_get(const char *body, const char *key, char *out, size_t cap) {
  if (!body || !key || !out || cap == 0) return false;
  size_t klen = strlen(key);
  const char *p = body;
  while (*p) {
    const char *amp = strchr(p, '&');
    const char *end = amp ? amp : p + strlen(p);
    if ((size_t)(end - p) > klen && strncmp(p, key, klen) == 0 && p[klen] == '=') {
      size_t n = 0;
      for (const char *q = p + klen + 1; q < end; q++) {
        char c = *q;
        if (c == '+') {
          c = ' ';
        } else if (c == '%') {
          if (end - q < 3) return false;
          int h = hexval(q[1]), l = hexval(q[2]);
          if (h < 0 || l < 0) return false;
          c = (char)(h * 16 + l);
          q += 2;
          if (c == 0) return false;  // an embedded NUL can never be meant
        }
        if (n + 1 >= cap) return false;
        out[n++] = c;
      }
      out[n] = 0;
      return true;
    }
    if (!amp) break;
    p = amp + 1;
  }
  return false;
}

bool wifi_qr_text(const char *ssid, const char *password, char *out, size_t cap) {
  size_t n = 0;
  auto put = [&](char c) {
    if (n + 1 >= cap) return false;
    out[n++] = c;
    return true;
  };
  auto put_esc = [&](const char *s) {
    for (; *s; s++) {
      if (strchr("\\;,:\"", *s) && !put('\\')) return false;
      if (!put(*s)) return false;
    }
    return true;
  };
  for (const char *s = "WIFI:T:WPA;S:"; *s; s++)
    if (!put(*s)) return false;
  if (!put_esc(ssid)) return false;
  for (const char *s = ";P:"; *s; s++)
    if (!put(*s)) return false;
  if (!put_esc(password)) return false;
  if (!put(';') || !put(';')) return false;
  out[n] = 0;
  return true;
}

}  // namespace cb
