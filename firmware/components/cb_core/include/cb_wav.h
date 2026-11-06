// cb_wav.h - RIFF/WAVE parser for the tone files (Softwarearchitektur 5.5). Hardware-free, PC-tested and
// fuzzed with damaged files (test/host/test_audio.cpp): a broken file must lead to "no sound", never to a
// crash - "Audio ist nie ein Grund für einen Absturz".
//
// Accepted: PCM (format 1), 16 bit, mono or stereo, 8 000 - 48 000 Hz. Unknown chunks (LIST, fact, ...)
// are skipped, odd chunk sizes respect the RIFF pad byte, a truncated data chunk is clipped to what is
// really in the file and to whole frames.
#pragma once

#include <cstddef>
#include <cstdint>

namespace cb {

// Where the bytes come from: a file on LittleFS on the device, a memory buffer in the tests.
class ByteSource {
 public:
  virtual ~ByteSource() = default;
  virtual uint32_t size() = 0;
  // Reads up to n bytes at offset; returns the number actually read.
  virtual size_t read_at(uint32_t offset, uint8_t *buf, size_t n) = 0;
};

struct WavInfo {
  uint32_t sample_rate;
  uint8_t channels;      // 1 or 2
  uint32_t data_offset;  // first sample byte
  uint32_t data_len;     // bytes of whole frames (multiple of 2 * channels)
};

enum class WavError : uint8_t {
  Ok,
  TooShort,
  NotRiff,
  NotWave,
  NoFmt,       // no fmt chunk before the data chunk
  NotPcm,      // compressed or float
  Not16Bit,
  BadChannels,
  BadRate,
  NoData,      // no data chunk, or it holds no complete frame
};

WavError parse_wav(ByteSource &src, WavInfo *info);
const char *wav_error_name(WavError e);

}  // namespace cb
