// test_audio.cpp - WAV parser (incl. a small fuzzer) and audio logic (Softwarearchitektur 11.2: "WAV-Parser gegen
// unbekannte Chunks, ungerade Längen, falsche Bittiefe, abgeschnittene Dateien - kein Absturz, sauberes Ablehnen").
#include <cstring>
#include <vector>

#include "cb_audio_logic.h"
#include "cb_bytes.h"
#include "cb_wav.h"
#include "mini_test.h"

using namespace cb;

namespace {

struct MemSource : ByteSource {
  std::vector<uint8_t> d;
  uint32_t size() override { return (uint32_t)d.size(); }
  size_t read_at(uint32_t off, uint8_t *buf, size_t n) override {
    if (off >= d.size()) return 0;
    size_t k = d.size() - off < n ? d.size() - off : n;
    memcpy(buf, d.data() + off, k);
    return k;
  }
};

void put4(std::vector<uint8_t> &v, const char *s) { v.insert(v.end(), s, s + 4); }
void put32(std::vector<uint8_t> &v, uint32_t x) {
  uint8_t b[4];
  put_u32le(b, x);
  v.insert(v.end(), b, b + 4);
}
void put16(std::vector<uint8_t> &v, uint16_t x) {
  uint8_t b[2];
  put_u16le(b, x);
  v.insert(v.end(), b, b + 2);
}

void fmt_chunk(std::vector<uint8_t> &v, uint16_t format, uint16_t ch, uint32_t rate, uint16_t bits) {
  put4(v, "fmt ");
  put32(v, 16);
  put16(v, format);
  put16(v, ch);
  put32(v, rate);
  put32(v, rate * ch * bits / 8);
  put16(v, (uint16_t)(ch * bits / 8));
  put16(v, bits);
}

// A complete file: RIFF header, optional junk chunk of odd size before fmt, fmt, data with `samples` samples.
MemSource make_wav(uint16_t ch = 1, uint32_t rate = 44100, uint16_t bits = 16, uint32_t samples = 100,
                   bool junk = false, uint16_t format = 1) {
  std::vector<uint8_t> v;
  put4(v, "RIFF");
  put32(v, 0);  // patched below
  put4(v, "WAVE");
  if (junk) {
    put4(v, "LIST");
    put32(v, 5);  // odd: followed by one pad byte
    v.insert(v.end(), {1, 2, 3, 4, 5, 0});
  }
  fmt_chunk(v, format, ch, rate, bits);
  put4(v, "data");
  put32(v, samples * 2);
  for (uint32_t i = 0; i < samples; i++) put16(v, (uint16_t)(i * 300));
  put_u32le(v.data() + 4, (uint32_t)v.size() - 8);
  MemSource m;
  m.d = v;
  return m;
}

}  // namespace

TEST(wav_mono_ok) {
  MemSource m = make_wav();
  WavInfo w{};
  CHECK(parse_wav(m, &w) == WavError::Ok);
  CHECK_EQ(w.sample_rate, 44100);
  CHECK_EQ(w.channels, 1);
  CHECK_EQ(w.data_len, 200);
  CHECK_EQ(w.data_offset, 44);
}

TEST(wav_unknown_odd_chunk_skipped_with_pad) {
  MemSource m = make_wav(2, 22050, 16, 100, true);
  WavInfo w{};
  CHECK(parse_wav(m, &w) == WavError::Ok);
  CHECK_EQ(w.channels, 2);
  CHECK_EQ(w.data_offset, 44 + 14);  // 8 header + 5 body + 1 pad
  CHECK_EQ(w.data_len, 200);         // 100 samples = 50 stereo frames
}

TEST(wav_rejects_wrong_formats) {
  WavInfo w{};
  MemSource a = make_wav(1, 44100, 8);
  CHECK(parse_wav(a, &w) == WavError::Not16Bit);
  MemSource b = make_wav(1, 44100, 16, 100, false, 3);  // IEEE float
  CHECK(parse_wav(b, &w) == WavError::NotPcm);
  MemSource c = make_wav(3);
  CHECK(parse_wav(c, &w) == WavError::BadChannels);
  MemSource d = make_wav(1, 96000);
  CHECK(parse_wav(d, &w) == WavError::BadRate);
  MemSource e = make_wav();
  e.d[0] = 'X';
  CHECK(parse_wav(e, &w) == WavError::NotRiff);
  MemSource f = make_wav();
  f.d[8] = 'X';
  CHECK(parse_wav(f, &w) == WavError::NotWave);
  MemSource g;
  g.d = {'R', 'I', 'F'};
  CHECK(parse_wav(g, &w) == WavError::TooShort);
}

TEST(wav_truncated_data_is_clipped_to_whole_frames) {
  MemSource m = make_wav(2);
  m.d.resize(m.d.size() - 3);  // cut in the middle of a frame
  WavInfo w{};
  CHECK(parse_wav(m, &w) == WavError::Ok);
  CHECK_EQ(w.data_len % 4, 0);
  CHECK(w.data_offset + w.data_len <= m.d.size());
}

TEST(wav_data_before_fmt_and_missing_data) {
  std::vector<uint8_t> v;
  put4(v, "RIFF");
  put32(v, 100);
  put4(v, "WAVE");
  put4(v, "data");
  put32(v, 4);
  put32(v, 0);
  MemSource m;
  m.d = v;
  WavInfo w{};
  CHECK(parse_wav(m, &w) == WavError::NoFmt);
  MemSource n = make_wav();
  n.d.resize(44 - 8);  // fmt only, data chunk cut away
  CHECK(parse_wav(n, &w) == WavError::NoData);
}

TEST(wav_huge_chunk_size_does_not_wrap) {
  MemSource m = make_wav(1, 44100, 16, 100, true);
  put_u32le(m.d.data() + 16, 0xFFFFFFFFu);  // LIST size: would wrap a 32-bit offset
  WavInfo w{};
  WavError e = parse_wav(m, &w);
  CHECK(e != WavError::Ok);
}

// Fuzzer: thousands of randomly damaged and truncated variants. The parser may reject anything, but it must
// return, and an accepted file must describe data that lies completely inside the file.
TEST(wav_fuzz_never_out_of_bounds) {
  MemSource base = make_wav(2, 44100, 16, 64, true);
  uint64_t rng = 0x9E3779B97F4A7C15ULL;
  auto next = [&]() {
    rng ^= rng << 13;
    rng ^= rng >> 7;
    rng ^= rng << 17;
    return rng;
  };
  int ok = 0, bad_bounds = 0;
  for (int iter = 0; iter < 20000; iter++) {
    MemSource m = base;
    int flips = 1 + (int)(next() % 6);
    for (int k = 0; k < flips; k++) m.d[next() % m.d.size()] = (uint8_t)next();
    if (next() % 3 == 0) m.d.resize(next() % (m.d.size() + 1));
    WavInfo w{};
    if (parse_wav(m, &w) == WavError::Ok) {
      ok++;
      if ((uint64_t)w.data_offset + w.data_len > m.d.size() || w.data_len == 0) bad_bounds++;
    }
  }
  CHECK_EQ(bad_bounds, 0);
  CHECK(ok > 100);  // the fuzzer also produced files that were still valid, so both paths were exercised
}

TEST(audio_priority) {
  CHECK(sound_preempts(Sound::Feedback, false, Sound::Ring));  // nothing playing: always
  CHECK(sound_preempts(Sound::Ring, true, Sound::Success));
  CHECK(sound_preempts(Sound::Ring, true, Sound::Ring));        // a new ring restarts the ring
  CHECK(sound_preempts(Sound::Error, true, Sound::NoNetwork));  // same rank
  CHECK(!sound_preempts(Sound::Feedback, true, Sound::Ring));
  CHECK(!sound_preempts(Sound::Success, true, Sound::Error));
}

TEST(audio_volume_steps_and_40_percent_cap) {
  CHECK_EQ(volume_next(15), 30);
  CHECK_EQ(volume_next(100), 15);
  CHECK_EQ(volume_next(42), 15);
  CHECK(volume_valid(75) && !volume_valid(40) && !volume_valid(0));
  CHECK_EQ(volume_gain_q15(100), 13107);  // 40 % of 32768: the loudest level the amplifier reproduces cleanly
  CHECK_EQ(volume_gain_q15(50), 6553);
  CHECK_EQ(volume_gain_q15(255), 13107);  // never above the cap
}

TEST(audio_scale_mono_to_stereo_saturates) {
  int16_t in[3] = {32767, -32768, 1000};
  int16_t out[6];
  scale_to_stereo(in, 3, true, 32768 * 2, out);  // gain 2.0: must clip, not wrap
  CHECK(out[0] == 32767 && out[1] == 32767);
  CHECK(out[2] == -32768 && out[3] == -32768);
  CHECK(out[4] == 2000 && out[5] == 2000);
  int16_t st[2] = {1000, -1000}, o2[2];
  scale_to_stereo(st, 2, false, volume_gain_q15(100), o2);
  // 1000 * 0.4 = 399.99..: the Q15 shift rounds toward minus infinity -> 399 and -400 (inaudible).
  CHECK(o2[0] == 399 && o2[1] == -400);
}

// ---- Level measurement for the acoustic self-check ---------------------------------------------------------
#include <cmath>
#include "cb_dsp.h"

TEST(dsp_sine_levels) {
  const uint32_t rate = 24000;
  std::vector<int16_t> s(4800);  // 200 ms
  for (size_t i = 0; i < s.size(); i++)  // -6 dBFS sine at 2000 Hz plus a DC offset like a PDM microphone
    s[i] = (int16_t)(500 + 16384 * std::sin(2 * 3.14159265358979323846 * 2000.0 * i / rate));
  float rms = rms_dbfs(s.data(), s.size());
  float at = tone_dbfs(s.data(), s.size(), rate, 2000);
  float off = tone_dbfs(s.data(), s.size(), rate, 1000);
  // A -6 dBFS (amplitude 0.5) sine has an RMS of 0.354 = -9.0 dBFS.
  CHECK(std::fabs(rms - (-9.03f)) < 0.2f);
  CHECK(std::fabs(at - (-9.03f)) < 0.3f);
  CHECK(off < at - 40);  // another frequency is far below
}

TEST(dsp_silence_floor) {
  std::vector<int16_t> s(1000, 123);  // pure DC
  CHECK(rms_dbfs(s.data(), s.size()) <= -119.0f);
}
