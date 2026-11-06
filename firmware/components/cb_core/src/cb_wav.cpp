// cb_wav.cpp - see cb_wav.h. All offsets are checked against the real file size before use; chunk sizes
// from the file are never trusted (64-bit arithmetic, so a size of 0xFFFFFFFF cannot wrap around).
#include "cb_wav.h"

#include <cstring>

#include "cb_bytes.h"

namespace cb {

WavError parse_wav(ByteSource &src, WavInfo *info) {
  const uint32_t fsize = src.size();
  uint8_t h[12];
  if (fsize < 12 || src.read_at(0, h, 12) != 12) return WavError::TooShort;
  if (memcmp(h, "RIFF", 4) != 0) return WavError::NotRiff;
  if (memcmp(h + 8, "WAVE", 4) != 0) return WavError::NotWave;

  bool have_fmt = false;
  uint64_t off = 12;
  for (int chunks = 0; chunks < 64 && off + 8 <= fsize; chunks++) {
    uint8_t ch[8];
    if (src.read_at((uint32_t)off, ch, 8) != 8) return WavError::TooShort;
    uint32_t csize = get_u32le(ch + 4);
    uint64_t body = off + 8;

    if (memcmp(ch, "fmt ", 4) == 0) {
      uint8_t f[16];
      if (csize < 16 || body + 16 > fsize || src.read_at((uint32_t)body, f, 16) != 16) return WavError::NoFmt;
      uint16_t format = get_u16le(f), channels = get_u16le(f + 2), bits = get_u16le(f + 14);
      uint32_t rate = get_u32le(f + 4);
      if (format != 1) return WavError::NotPcm;
      if (bits != 16) return WavError::Not16Bit;
      if (channels != 1 && channels != 2) return WavError::BadChannels;
      if (rate < 8000 || rate > 48000) return WavError::BadRate;
      info->sample_rate = rate;
      info->channels = (uint8_t)channels;
      have_fmt = true;
    } else if (memcmp(ch, "data", 4) == 0) {
      if (!have_fmt) return WavError::NoFmt;
      uint64_t avail = fsize > body ? fsize - body : 0;
      uint64_t len = csize < avail ? csize : avail;  // truncated file: play what is there
      uint32_t frame = 2u * info->channels;
      len -= len % frame;
      if (len == 0) return WavError::NoData;
      info->data_offset = (uint32_t)body;
      info->data_len = (uint32_t)len;
      return WavError::Ok;
    }
    off = body + csize + (csize & 1);  // RIFF: chunks are padded to even length
  }
  return have_fmt ? WavError::NoData : WavError::NoFmt;
}

const char *wav_error_name(WavError e) {
  switch (e) {
    case WavError::Ok: return "ok";
    case WavError::TooShort: return "file too short";
    case WavError::NotRiff: return "not RIFF";
    case WavError::NotWave: return "not WAVE";
    case WavError::NoFmt: return "no fmt chunk";
    case WavError::NotPcm: return "not PCM";
    case WavError::Not16Bit: return "not 16 bit";
    case WavError::BadChannels: return "not mono/stereo";
    case WavError::BadRate: return "sample rate outside 8-48 kHz";
    case WavError::NoData: return "no audio data";
  }
  return "?";
}

}  // namespace cb
