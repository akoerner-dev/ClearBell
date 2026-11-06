// cb_audio_logic.h - which tone wins, how loud, and the sample arithmetic. Hardware-free, PC-tested.
//
// Priority (Softwarearchitektur 5.5): Ring > Error = NoNetwork > Success > Feedback. A request of equal or
// higher priority replaces the running tone; a lower one is dropped (a "tick" must never cut off a ring).
//
// Volume: the user sees the V0.1 steps 15 / 30 / 50 / 75 / 100 %. The MAX98357A at 3.3 V clips above about
// 40 % digital level (bring-up finding AU-B-02_Klirren_Clipping / IN-B-02), and 100 % digital is only
// 2.6 dB louder than 40 % but heavily distorted. Therefore the user scale maps onto 0-40 % digital:
// 100 % user = 40 % digital = the loudest clean level. kMaxDigitalPercent is the single place to change.
#pragma once

#include <cstddef>
#include <cstdint>

namespace cb {

enum class Sound : uint8_t {
  Ring,       // indoor: a visitor rang
  Error,      // ring not acknowledged / door command failed
  NoNetwork,  // outdoor without WiFi: distinguishable hint, NOT a ring confirmation (F-4)
  Success,    // ring acknowledged / door opened
  Feedback,   // short tick: volume step, "you may release now" for the 3-s hold
  Count
};

const char *sound_name(Sound s);
const char *sound_file(Sound s);  // path on LittleFS, e.g. "/klingel.wav"
int sound_priority(Sound s);      // higher wins

// Should `incoming` start while `current` is playing (playing == false: nothing plays)?
bool sound_preempts(Sound incoming, bool playing, Sound current);

constexpr int kMaxDigitalPercent = 40;
constexpr uint8_t kVolumeSteps[] = {15, 30, 50, 75, 100};
constexpr int kVolumeStepCount = sizeof(kVolumeSteps) / sizeof(kVolumeSteps[0]);

// Next step after `percent` (wraps 100 -> 15). An unknown value starts at the first step.
uint8_t volume_next(uint8_t percent);
// True for exactly the five allowed steps (values read from NVS are checked with this).
bool volume_valid(uint8_t percent);
// Digital gain in Q15 (32768 = 1.0) for a user percentage: percent * kMaxDigitalPercent / 100.
int32_t volume_gain_q15(uint8_t percent);

// Scales n samples from `in` by gain (Q15, saturating) into `out`. Mono: each sample becomes a stereo frame,
// `out` receives 2n samples. Stereo: `in` already holds frames, `out` receives n samples. The amplifier is
// fed stereo frames and mixes L/R itself (its SD_MODE resistor network selects (L+R)/2).
void scale_to_stereo(const int16_t *in, size_t n, bool mono, int32_t gain_q15, int16_t *out);

}  // namespace cb
