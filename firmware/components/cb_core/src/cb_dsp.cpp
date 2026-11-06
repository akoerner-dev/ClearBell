// cb_dsp.cpp - see cb_dsp.h.
#include "cb_dsp.h"

#include <cmath>

namespace cb {

namespace {
constexpr double kPi = 3.14159265358979323846;  // M_PI is not standard C++
constexpr float kFloor = -120.0f;  // returned for digital silence instead of -infinity

float mean(const int16_t *s, size_t n) {
  double m = 0;
  for (size_t i = 0; i < n; i++) m += s[i];
  return n ? (float)(m / n) : 0.0f;
}

float to_db(double rms) {
  return rms > 0 ? (float)(20.0 * std::log10(rms / 32768.0)) : kFloor;
}
}  // namespace

float rms_dbfs(const int16_t *s, size_t n) {
  if (!n) return kFloor;
  float dc = mean(s, n);
  double acc = 0;
  for (size_t i = 0; i < n; i++) {
    double v = s[i] - dc;
    acc += v * v;
  }
  return to_db(std::sqrt(acc / n));
}

float tone_dbfs(const int16_t *s, size_t n, uint32_t sample_rate, float freq_hz) {
  if (!n || !sample_rate) return kFloor;
  float dc = mean(s, n);
  // Goertzel: one DFT bin at freq_hz. |X|^2 * 2/n^2 = amplitude^2 / ... -> RMS of that component.
  double w = 2.0 * kPi * freq_hz / sample_rate;
  double coeff = 2.0 * std::cos(w), q1 = 0, q2 = 0;
  for (size_t i = 0; i < n; i++) {
    double q0 = coeff * q1 - q2 + (s[i] - dc);
    q2 = q1;
    q1 = q0;
  }
  double power = q1 * q1 + q2 * q2 - coeff * q1 * q2;  // |X(f)|^2
  double amplitude = 2.0 * std::sqrt(power > 0 ? power : 0) / n;
  return to_db(amplitude / std::sqrt(2.0));  // RMS of a sine with that amplitude
}

}  // namespace cb
