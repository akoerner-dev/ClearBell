// cb_json.h - a tiny JSON writer into a fixed buffer, for the status page. Header-only, PC-tested.
//
// No heap, no library: the status snapshot is a few kilobytes and its shape is fixed. Commas are inserted
// automatically; strings are escaped. If the buffer is too small the output is cut and ok() turns false -
// the page then shows an error instead of half a document.
//
//   cb::Json j(buf, sizeof buf);
//   j.begin_obj();  j.key("rssi"); j.num(-61);  j.key("ssid_hidden"); j.boolean(true);  j.end_obj();
#pragma once

#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace cb {

class Json {
 public:
  Json(char *buf, size_t cap) : buf_(buf), cap_(cap) {
    if (cap_) buf_[0] = 0;
  }

  void begin_obj() { open('{'); }
  void end_obj() { close('}'); }
  void begin_arr() { open('['); }
  void end_arr() { close(']'); }

  void key(const char *k) {
    sep();
    str_raw(k);
    put(':');
    after_key_ = true;
  }
  void str(const char *s) {
    sep();
    str_raw(s ? s : "");
  }
  void num(long v) {
    sep();
    printf_raw("%ld", v);
  }
  void unum(unsigned long v) {
    sep();
    printf_raw("%lu", v);
  }
  void real(double v, int decimals) {
    sep();
    printf_raw("%.*f", decimals, v);
  }
  void boolean(bool v) {
    sep();
    printf_raw("%s", v ? "true" : "false");
  }
  void null() {
    sep();
    printf_raw("null");
  }

  // Shorthands for "key": value.
  void kv(const char *k, const char *v) { key(k), str(v); }
  void kv(const char *k, long v) { key(k), num(v); }
  void kv(const char *k, int v) { key(k), num(v); }
  void kv(const char *k, unsigned long v) { key(k), unum(v); }
  void kv(const char *k, unsigned v) { key(k), unum(v); }
  void kv(const char *k, bool v) { key(k), boolean(v); }

  bool ok() const { return ok_ && depth_ == 0; }
  size_t len() const { return len_; }
  const char *c_str() const { return buf_; }

 private:
  void put(char c) {
    if (len_ + 1 < cap_) {
      buf_[len_++] = c;
      buf_[len_] = 0;
    } else {
      ok_ = false;
    }
  }
  void printf_raw(const char *fmt, ...) __attribute__((format(printf, 2, 3))) {
    if (len_ >= cap_) {
      ok_ = false;
      return;
    }
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf_ + len_, cap_ - len_, fmt, ap);
    va_end(ap);
    if (n < 0 || (size_t)n >= cap_ - len_) {
      ok_ = false;
      len_ = cap_ - 1;
      buf_[len_] = 0;
    } else {
      len_ += (size_t)n;
    }
  }
  void str_raw(const char *s) {
    put('"');
    for (; *s; s++) {
      unsigned char c = (unsigned char)*s;
      if (c == '"' || c == '\\') {
        put('\\');
        put((char)c);
      } else if (c < 0x20) {
        printf_raw("\\u%04x", c);
      } else {
        put((char)c);
      }
    }
    put('"');
  }
  void sep() {
    if (after_key_) {
      after_key_ = false;
      return;
    }
    if (depth_ > 0 && need_comma_[depth_]) put(',');
    if (depth_ > 0) need_comma_[depth_] = true;
  }
  void open(char c) {
    sep();
    put(c);
    if (depth_ + 1 < kMaxDepth) {
      depth_++;
      need_comma_[depth_] = false;
    } else {
      ok_ = false;
    }
  }
  void close(char c) {
    put(c);
    if (depth_ > 0) depth_--;
    else ok_ = false;
  }

  static constexpr int kMaxDepth = 8;
  char *buf_;
  size_t cap_;
  size_t len_ = 0;
  int depth_ = 0;
  bool need_comma_[kMaxDepth] = {};
  bool after_key_ = false;
  bool ok_ = true;
};

}  // namespace cb
