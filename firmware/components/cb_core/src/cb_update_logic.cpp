// cb_update_logic.cpp - see cb_update_logic.h.
//
// Layout of an ESP-IDF app image (esp_app_format.h), little endian:
//   0   esp_image_header_t (24 B): magic 0xE9, segment count, ..., chip_id (u16) at offset 12
//   24  esp_image_segment_header_t (8 B) of the first segment (.flash.appdesc)
//   32  esp_app_desc_t: magic 0xABCD5432, secure_version, 2 reserved, version[32], project_name[32], ...
#include "cb_update_logic.h"

#include <cstring>

#include "cb_bytes.h"

namespace cb {

namespace {
constexpr uint8_t kImageMagic = 0xE9;
constexpr uint32_t kAppDescMagic = 0xABCD5432;
constexpr size_t kDesc = 32;
constexpr size_t kVersionOff = kDesc + 16;
constexpr size_t kProjectOff = kVersionOff + 32;

// Copies a fixed 32-byte field; false if it has no terminating zero (= not a description).
bool field(const uint8_t *p, char out[33]) {
  memcpy(out, p, 32);
  out[32] = 0;
  return memchr(p, 0, 32) != nullptr;
}
}  // namespace

const char *image_check_name(ImageCheck c) {
  switch (c) {
    case ImageCheck::Ok: return "ok";
    case ImageCheck::TooShort: return "too short";
    case ImageCheck::NotAppImage: return "not an ESP firmware image";
    case ImageCheck::WrongChip: return "image for another chip";
    case ImageCheck::NoAppDesc: return "no app description";
    case ImageCheck::WrongProject: return "not a ClearBell firmware";
    case ImageCheck::WrongUnit: return "firmware for the other unit type";
  }
  return "?";
}

ImageCheck check_image_header(const uint8_t *b, size_t n, const char *unit, char version_out[33]) {
  version_out[0] = 0;
  if (!b || n < kImageHeaderNeed) return ImageCheck::TooShort;
  if (b[0] != kImageMagic || b[1] == 0 || b[1] > 16) return ImageCheck::NotAppImage;
  if (get_u16le(b + 12) != kChipIdEsp32S3) return ImageCheck::WrongChip;
  if (get_u32le(b + kDesc) != kAppDescMagic) return ImageCheck::NoAppDesc;
  char version[33], project[33];
  if (!field(b + kVersionOff, version) || !field(b + kProjectOff, project)) return ImageCheck::NoAppDesc;
  memcpy(version_out, version, 33);
  if (strcmp(project, "clearbell") != 0) return ImageCheck::WrongProject;
  // Unit type as SemVer build metadata: "...+aussen" / "...+innen" at the very end.
  const char *plus = strrchr(version, '+');
  if (!plus || strcmp(plus + 1, unit) != 0) return ImageCheck::WrongUnit;
  return ImageCheck::Ok;
}

UpdateVerdict update_verdict(const UpdateHealth &h) {
  if (h.selftest_run && !h.selftest_ok) return UpdateVerdict::Rollback;  // crypto broken: never keep it
  bool peers_ok = (h.peers_now & h.peers_before) == h.peers_before;
  bool healthy = h.selftest_ok && h.wifi_up && h.web_up && peers_ok;
  if (healthy && h.uptime_ms >= kProbationMinMs) return UpdateVerdict::Confirm;
  if (h.uptime_ms >= kProbationMaxMs) return UpdateVerdict::Rollback;
  return UpdateVerdict::Wait;
}

}  // namespace cb
