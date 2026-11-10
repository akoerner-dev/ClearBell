// cb_update_logic.h - firmware updates (port step 10), hardware-free, PC-tested.
//
// Updates come as a SIGNED image uploaded on the status page in maintenance mode (no PC tool on the buyer's side).
// The signature itself is checked in cb_update_sig.h (port step 10d, R26). Three decisions live here:
//
// 1. check_image_header(): BEFORE a single byte goes into the passive slot, the first kilobyte of the upload must
//    be an ESP32-S3 app image of project "clearbell" for THIS unit type (version "x.y.z+aussen" / "+innen", set
//    by CMakeLists.txt). The integrity of the whole image (SHA-256 appended by the build) is checked afterwards by
//    esp_ota_end(); a wrong or truncated file never becomes bootable.
//
// 2. version_older(): downgrade protection (below).
//
// 3. update_verdict(): a freshly installed image runs "on probation" (the bootloader keeps the old one). It is
//    confirmed only when the self-test passed, WiFi is up, the status page (= the update path) runs and every peer
//    that answered before the update answers again - otherwise it is rolled back after kProbationMaxMs. A crash
//    or watchdog reset during probation makes the bootloader return to the old image by itself.
#pragma once

#include <cstddef>
#include <cstdint>

namespace cb {

constexpr size_t kImageHeaderNeed = 32 + 256;   // image header + first segment header + app description
constexpr uint16_t kChipIdEsp32S3 = 9;          // esp_chip_id_t ESP_CHIP_ID_ESP32S3
constexpr uint32_t kProbationMinMs = 60 * 1000;     // run at least this long before confirming
constexpr uint32_t kProbationMaxMs = 5 * 60 * 1000; // not healthy by then -> back to the old image

enum class ImageCheck : uint8_t { Ok, TooShort, NotAppImage, WrongChip, NoAppDesc, WrongProject, WrongUnit };
const char *image_check_name(ImageCheck c);

// b/n: the first bytes of the upload (n >= kImageHeaderNeed). unit: "aussen" or "innen". version_out receives the
// image's version string (e.g. "0.2.1+innen") when the description could be read.
ImageCheck check_image_header(const uint8_t *b, size_t n, const char *unit, char version_out[33]);

// Downgrade protection (port step 10d, R26): is `candidate` an OLDER firmware than `running`? Versions are SemVer
// "major.minor.patch[-pre][+unit]"; the unit part is ignored, a pre-release is older than its release
// ("0.2.0-dev" < "0.2.0"), two pre-releases compare as text. A version that cannot be parsed counts as older, so it
// needs the explicit "allow older version" of the maintenance page. Reinstalling the same version is not "older".
bool version_older(const char *candidate, const char *running);

enum class UpdateVerdict : uint8_t { Wait, Confirm, Rollback };

struct UpdateHealth {
  uint32_t uptime_ms;
  bool selftest_run, selftest_ok;
  bool wifi_up;
  bool web_up;
  uint8_t peers_before;  // bit per UnitId: peers reachable when the update was installed
  uint8_t peers_now;     // bit per UnitId: reachable now
};
UpdateVerdict update_verdict(const UpdateHealth &h);

}  // namespace cb
