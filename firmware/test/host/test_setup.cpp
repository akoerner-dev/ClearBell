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
