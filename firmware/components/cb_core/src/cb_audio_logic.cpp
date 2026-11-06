// cb_audio_logic.cpp - see cb_audio_logic.h.
#include "cb_audio_logic.h"

namespace cb {

const char *sound_name(Sound s) {
  switch (s) {
    case Sound::Ring: return "ring";
    case Sound::Error: return "error";
    case Sound::NoNetwork: return "no-network";
    case Sound::Success: return "success";
    case Sound::Feedback: return "feedback";
    default: return "?";
  }
}

const char *sound_file(Sound s) {
  switch (s) {
    case Sound::Ring: return "/klingel.wav";
    case Sound::Error: return "/fehler.wav";
    case Sound::NoNetwork: return "/kein_netz.wav";
    case Sound::Success: return "/ok.wav";
    case Sound::Feedback: return "/tipp.wav";
    default: return "";
  }
}

int sound_priority(Sound s) {
  switch (s) {
    case Sound::Ring: return 4;
    case Sound::Error:
    case Sound::NoNetwork: return 3;
    case Sound::Success: return 2;
    case Sound::Feedback: return 1;
    default: return 0;
  }
}

bool sound_preempts(Sound incoming, bool playing, Sound current) {
  return !playing || sound_priority(incoming) >= sound_priority(current);
}

bool volume_valid(uint8_t percent) {
  for (uint8_t v : kVolumeSteps)
    if (v == percent) return true;
  return false;
}

uint8_t volume_next(uint8_t percent) {
  for (int i = 0; i < kVolumeStepCount; i++)
    if (kVolumeSteps[i] == percent) return kVolumeSteps[(i + 1) % kVolumeStepCount];
  return kVolumeSteps[0];
}

int32_t volume_gain_q15(uint8_t percent) {
  if (percent > 100) percent = 100;
  return (int32_t)percent * kMaxDigitalPercent * 32768 / 10000;
}

void scale_to_stereo(const int16_t *in, size_t n, bool mono, int32_t gain_q15, int16_t *out) {
  for (size_t i = 0; i < n; i++) {
    int32_t v = ((int32_t)in[i] * gain_q15) >> 15;
    if (v > 32767) v = 32767;
    if (v < -32768) v = -32768;
    if (mono) {
      out[2 * i] = (int16_t)v;
      out[2 * i + 1] = (int16_t)v;
    } else {
      out[i] = (int16_t)v;
    }
  }
}

}  // namespace cb
