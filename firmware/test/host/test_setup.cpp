// test_setup.cpp - port step 10a: device code, form decoding, WiFi QR text (cb_setup_logic.h).
#include <cstring>

#include "cb_setup_logic.h"
#include "mini_test.h"

using namespace cb;

TEST(device_code_generation_and_grouping) {
  uint8_t zero[10] = {};
  uint8_t ones[10];
  memset(ones, 0xFF, sizeof ones);
  char c[17], g[20];
  device_code_from_bytes(zero, c);
  CHECK(strcmp(c, "0000000000000000") == 0);
  device_code_from_bytes(ones, c);
  CHECK(strcmp(c, "ZZZZZZZZZZZZZZZZ") == 0);
  uint8_t mix[10] = {0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF, 0x10, 0x32};
  device_code_from_bytes(mix, c);  // python reference: Crockford Base32 of 0x0123456789abcdef1032
  CHECK(strcmp(c, "04HMASW9NF6YY41J") == 0);
  device_code_group(c, g);
  CHECK(strcmp(g, "04HM-ASW9-NF6Y-Y41J") == 0);
}

TEST(device_code_typing_is_forgiving) {
  char c[17];
  CHECK(device_code_normalize("04hm-asw9-nf6y-y0sj", c));
  CHECK(strcmp(c, "04HMASW9NF6YY0SJ") == 0);
  CHECK(device_code_normalize("O4HM ASW9 NF6Y YOSJ", c));  // O typed for 0
  CHECK(strcmp(c, "04HMASW9NF6YY0SJ") == 0);
  CHECK(device_code_normalize("IIII LLLL 0000 0000", c));
  CHECK(strcmp(c, "1111111100000000") == 0);
  CHECK(!device_code_normalize("04HM-ASW9-NF6Y", c));         // too short
  CHECK(!device_code_normalize("04HM-ASW9-NF6Y-Y0SJ-X", c));  // too long
  CHECK(!device_code_normalize("04HM-ASW9-NF6Y-Y0SU", c));    // U is not in the alphabet
  CHECK(!device_code_normalize("04HM-ASW9-NF6Y-Y0S!", c));
  CHECK(!device_code_normalize(nullptr, c));
}

TEST(form_get_decodes_urlencoded) {
  char v[40];
  const char *body = "ssid=Mein+WLAN%21&pass=a%26b%3Dc&role=eg&empty=";
  CHECK(form_get(body, "ssid", v, sizeof v));
  CHECK(strcmp(v, "Mein WLAN!") == 0);
  CHECK(form_get(body, "pass", v, sizeof v));
  CHECK(strcmp(v, "a&b=c") == 0);
  CHECK(form_get(body, "role", v, sizeof v));
  CHECK(strcmp(v, "eg") == 0);
  CHECK(form_get(body, "empty", v, sizeof v));
  CHECK(v[0] == 0);
  CHECK(!form_get(body, "ss", v, sizeof v));   // prefix of a key is not the key
  CHECK(!form_get(body, "nope", v, sizeof v));
  CHECK(!form_get("a=%2", "a", v, sizeof v));  // truncated escape
  CHECK(!form_get("a=%zz", "a", v, sizeof v));
  CHECK(!form_get("a=%00x", "a", v, sizeof v));
  char small[4];
  CHECK(!form_get("a=abcdef", "a", small, sizeof small));
  CHECK(form_get("x=1&a=%C3%A4", "a", v, sizeof v));  // UTF-8 umlaut passes through
  CHECK(strcmp(v, "\xC3\xA4") == 0);
}

TEST(wifi_qr_text_escapes) {
  char q[100];
  CHECK(wifi_qr_text("ClearBell-f3ac94", "04HMASW9NF6YY0SJ", q, sizeof q));
  CHECK(strcmp(q, "WIFI:T:WPA;S:ClearBell-f3ac94;P:04HMASW9NF6YY0SJ;;") == 0);
  CHECK(wifi_qr_text("a;b", "c:d\\", q, sizeof q));
  CHECK(strcmp(q, "WIFI:T:WPA;S:a\\;b;P:c\\:d\\\\;;") == 0);
  char small[10];
  CHECK(!wifi_qr_text("ClearBell-f3ac94", "x", small, sizeof small));
}

// FW-B-21: free flat names - what a buyer types, and what must never reach a push or the outdoor unit.
TEST(flat_name_normalize_accepts_real_names) {
  char n[kFlatNameMaxBytes + 1];
  CHECK(flat_name_normalize("Familie Müller", n));
  CHECK(strcmp(n, "Familie Müller") == 0);
  CHECK(flat_name_normalize("  Praxis   Dr. Weiß \t", n));  // trimmed, inner runs folded
  CHECK(strcmp(n, "Praxis Dr. Weiß") == 0);
  CHECK(flat_name_normalize("Familie Strauß", n));  // ß: 2 bytes, 1 character
  CHECK(strcmp(n, "Familie Strauß") == 0);
  CHECK(flat_name_normalize("STRAUẞ", n));  // capital sharp s U+1E9E: 3 bytes
  CHECK(strcmp(n, "STRAUẞ") == 0);
  CHECK(flat_name_normalize("ßßßßßßßßßßßßßßßßßßßß", n));    // 20 × ß = 40 bytes: fits
  CHECK(!flat_name_normalize("ßßßßßßßßßßßßßßßßßßßßß", n));  // 21 × ß = 42 bytes
  CHECK(flat_name_normalize("Yılmaz & Söhne", n));
  CHECK(strcmp(n, "Yılmaz & Söhne") == 0);
  CHECK(flat_name_normalize("", n));  // empty = no own name, the default is shown
  CHECK(n[0] == 0);
  CHECK(flat_name_normalize("   ", n));
  CHECK(n[0] == 0);
  CHECK(flat_name_normalize("WG 2. OG <links>", n));  // the pages set names with textContent only
  CHECK(strcmp(n, "WG 2. OG <links>") == 0);
}

TEST(flat_name_normalize_limits) {
  char n[kFlatNameMaxBytes + 1];
  CHECK(flat_name_normalize("abcdefghijklmnopqrstuvwx", n));    // 24 characters
  CHECK(!flat_name_normalize("abcdefghijklmnopqrstuvwxy", n));  // 25
  // 20 umlauts = 20 characters but 40 bytes: still fits; 21 do not (42 bytes) although only 21 characters.
  CHECK(flat_name_normalize("ääääääääääääääääääää", n));
  CHECK(strlen(n) == 40);
  CHECK(!flat_name_normalize("äääääääääääääääääääää", n));
  CHECK(flat_name_normalize("\xF0\x9F\x94\x94 Klingel", n));  // emoji (4 bytes) is allowed
  CHECK(!flat_name_normalize(nullptr, n));
}

TEST(flat_name_normalize_rejects_broken_text) {
  char n[kFlatNameMaxBytes + 1];
  CHECK(!flat_name_normalize("A\x01" "B", n));        // control character
  CHECK(!flat_name_normalize("A\x7F", n));            // DEL
  CHECK(!flat_name_normalize("A\xC2\x85" "B", n));    // C1 control (NEL)
  CHECK(!flat_name_normalize("M\xFCller", n));        // Latin-1 byte, not UTF-8
  CHECK(!flat_name_normalize("\xC3", n));             // cut-off sequence
  CHECK(!flat_name_normalize("\xC0\xAF", n));         // overlong '/'
  CHECK(!flat_name_normalize("\xED\xA0\x80", n));     // UTF-16 surrogate
  CHECK(!flat_name_normalize("\xF4\x90\x80\x80", n)); // beyond U+10FFFF
}

TEST(flat_labels_and_url_encode) {
  CHECK(strcmp(flat_key_label(2), "Taste unten") == 0);
  CHECK(strcmp(flat_key_label(3), "Taste oben") == 0);
  CHECK(strcmp(flat_key_label(1), "") == 0);
  CHECK(strcmp(flat_default_name(2), "Wohnung unten") == 0);
  CHECK(strcmp(flat_default_name(3), "Wohnung oben") == 0);
  char u[64];
  CHECK(url_encode("ClearBell Familie Müller", u, sizeof u));
  CHECK(strcmp(u, "ClearBell%20Familie%20M%C3%BCller") == 0);
  CHECK(url_encode("Weiß ẞ", u, sizeof u));
  CHECK(strcmp(u, "Wei%C3%9F%20%E1%BA%9E") == 0);
  CHECK(url_encode("a-b_c.d~e&f", u, sizeof u));
  CHECK(strcmp(u, "a-b_c.d~e%26f") == 0);
  char small[6];
  CHECK(!url_encode("ä b", small, sizeof small));  // needs 9 + 1
  CHECK(url_encode("abcde", small, sizeof small));
  CHECK(!url_encode("abcdef", small, sizeof small));
}
