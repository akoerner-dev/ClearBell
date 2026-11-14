// test_update.cpp - port step 10: image header check and probation verdict (cb_update_logic.h).
#include <cstring>

#include "cb_bytes.h"
#include "cb_update_logic.h"
#include "mini_test.h"

using namespace cb;

namespace {

// A minimal image start as the ESP-IDF build writes it.
void make_image(uint8_t *b, const char *version, const char *project, uint16_t chip = kChipIdEsp32S3,
                uint32_t secure_version = 0) {
  memset(b, 0, kImageHeaderNeed);
  b[0] = 0xE9;
  b[1] = 6;  // segments
  put_u16le(b + 12, chip);
  put_u32le(b + 32, 0xABCD5432);
  put_u32le(b + 36, secure_version);
  strncpy(reinterpret_cast<char *>(b + 48), version, 31);
  strncpy(reinterpret_cast<char *>(b + 80), project, 31);
}

}  // namespace

// Härtung Stufe B (B2): the security version sits right after the description magic (esp_app_desc_t).
TEST(update_secure_version_read_and_compared) {
  uint8_t b[kImageHeaderNeed];
  char v[33];
  make_image(b, "0.2.1+innen", "clearbell", kChipIdEsp32S3, 3);
  CHECK(check_image_header(b, sizeof b, "innen", v) == ImageCheck::Ok);
  CHECK(image_secure_version(b) == 3);
  make_image(b, "0.2.1+innen", "clearbell");
  CHECK(image_secure_version(b) == 0);
  CHECK(secure_version_allowed(0, 0));
  CHECK(secure_version_allowed(2, 1));
  CHECK(secure_version_allowed(1, 1));
  CHECK(!secure_version_allowed(0, 1));  // an older security level never runs again after the eFuse bit
  CHECK(image_secure_version(nullptr) == 0);
}

TEST(update_header_accepts_matching_image) {
  uint8_t b[kImageHeaderNeed];
  char v[33];
  make_image(b, "0.2.1+innen", "clearbell");
  CHECK(check_image_header(b, sizeof b, "innen", v) == ImageCheck::Ok);
  CHECK(strcmp(v, "0.2.1+innen") == 0);
}

TEST(update_header_refuses_wrong_files) {
  uint8_t b[kImageHeaderNeed];
  char v[33];
  make_image(b, "0.2.1+aussen", "clearbell");
  CHECK(check_image_header(b, sizeof b, "innen", v) == ImageCheck::WrongUnit);
  make_image(b, "0.2.1", "clearbell");  // built without cb.py: no unit type -> refused
  CHECK(check_image_header(b, sizeof b, "innen", v) == ImageCheck::WrongUnit);
  make_image(b, "0.2.1+innen-x", "clearbell");
  CHECK(check_image_header(b, sizeof b, "innen", v) == ImageCheck::WrongUnit);
  make_image(b, "1.0+innen", "wled");
  CHECK(check_image_header(b, sizeof b, "innen", v) == ImageCheck::WrongProject);
  make_image(b, "0.2.1+innen", "clearbell", 0);  // ESP32 classic
  CHECK(check_image_header(b, sizeof b, "innen", v) == ImageCheck::WrongChip);
  make_image(b, "0.2.1+innen", "clearbell");
  CHECK(check_image_header(b, kImageHeaderNeed - 1, "innen", v) == ImageCheck::TooShort);
  b[32] = 0;  // app description magic broken
  CHECK(check_image_header(b, sizeof b, "innen", v) == ImageCheck::NoAppDesc);
  make_image(b, "0.2.1+innen", "clearbell");
  memset(b + 48, 'A', 32);  // version without terminating zero
  CHECK(check_image_header(b, sizeof b, "innen", v) == ImageCheck::NoAppDesc);
  uint8_t wav[kImageHeaderNeed] = {'R', 'I', 'F', 'F'};
  CHECK(check_image_header(wav, sizeof wav, "innen", v) == ImageCheck::NotAppImage);
  CHECK(check_image_header(nullptr, 0, "innen", v) == ImageCheck::TooShort);
}

TEST(update_verdict_probation) {
  UpdateHealth h{};
  h.selftest_run = h.selftest_ok = h.wifi_up = h.web_up = true;
  h.peers_before = 0b0100;  // EG answered before the update
  h.peers_now = 0b0100;
  h.uptime_ms = kProbationMinMs - 1;
  CHECK(update_verdict(h) == UpdateVerdict::Wait);  // healthy, but not long enough
  h.uptime_ms = kProbationMinMs;
  CHECK(update_verdict(h) == UpdateVerdict::Confirm);
  // the peer that answered before is missing now -> wait, then roll back
  h.peers_now = 0;
  CHECK(update_verdict(h) == UpdateVerdict::Wait);
  h.uptime_ms = kProbationMaxMs;
  CHECK(update_verdict(h) == UpdateVerdict::Rollback);
  // a peer that was already missing before (e.g. no OG unit) does not block
  h.peers_before = 0;
  CHECK(update_verdict(h) == UpdateVerdict::Confirm);
  // no WiFi -> never confirmed
  h.wifi_up = false;
  h.uptime_ms = kProbationMaxMs - 1;
  CHECK(update_verdict(h) == UpdateVerdict::Wait);
  h.uptime_ms = kProbationMaxMs;
  CHECK(update_verdict(h) == UpdateVerdict::Rollback);
  // broken crypto self-test -> back at once
  UpdateHealth s{};
  s.selftest_run = true;
  s.selftest_ok = false;
  s.uptime_ms = 1000;
  CHECK(update_verdict(s) == UpdateVerdict::Rollback);
  // self-test not run yet -> wait
  s.selftest_run = false;
  CHECK(update_verdict(s) == UpdateVerdict::Wait);
}

TEST(owner_change_waits_for_the_button) {
  // R32 (E4, VK-04, RV-05): own update key / "owner only" take effect only after a short press of the boot button at
  // the unit within 60 s - a request rewritten by someone in the WiFi, or a script injected into the page, cannot
  // set a key on its own.
  OwnerConfirm c;
  OwnerChange ch{};
  ch.set_key = true;
  memcpy(ch.key_hex, "ab", 3);
  CHECK(!c.pending(0));
  OwnerChange got{};
  CHECK(!c.take(0, &got));  // nothing armed: a press confirms nothing
  c.arm(ch, 1000);
  CHECK(c.pending(1000));
  CHECK(c.left_ms(1000) == kOwnerConfirmMs);
  CHECK(c.pending(1000 + kOwnerConfirmMs - 1));
  CHECK(!c.pending(1000 + kOwnerConfirmMs));  // 60 s passed: forgotten
  CHECK(!c.take(1000 + kOwnerConfirmMs, &got));
  c.arm(ch, 5000);
  CHECK(c.take(6000, &got));
  CHECK(got.set_key && strcmp(got.key_hex, "ab") == 0);
  CHECK(!c.pending(6000));
  CHECK(!c.take(6001, &got));  // used once
  OwnerChange other{};
  other.set_only = true;
  other.only = true;
  c.arm(ch, 7000);
  c.arm(other, 8000);  // a newer request replaces the older one (the page shows which one is waiting)
  CHECK(c.take(8500, &got));
  CHECK(!got.set_key && got.set_only && got.only);
  c.arm(ch, 9000);
  c.cancel();  // maintenance left
  CHECK(!c.pending(9000));
  c.arm(ch, 0xFFFFFF00u);  // across the millis wrap
  CHECK(c.pending(0x00000100u));
}
