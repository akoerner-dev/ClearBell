// cb_dsp.h - level measurements for the acoustic self-check ("audio-check"): the unit plays a tone through its
// own loudspeaker and listens with its own microphone. Hardware-free, PC-tested with synthetic signals.
//
//   rms_dbfs      overall level of a block, dB relative to full scale (0 dBFS = full-scale square wave)
//   tone_dbfs     level of ONE frequency (Goertzel filter), same scale as rms_dbfs for a pure sine
// Both remove the DC offset first (PDM microphones deliver a small constant offset).
#pragma once

#include <cstddef>
#include <cstdint>

namespace cb {

float rms_dbfs(const int16_t *s, size_t n);
float tone_dbfs(const int16_t *s, size_t n, uint32_t sample_rate, float freq_hz);

}  // namespace cb
