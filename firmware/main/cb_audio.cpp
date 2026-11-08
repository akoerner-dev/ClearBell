// cb_audio.cpp - see cb_audio.h.
#include "cb_audio.h"

#include <Arduino.h>
#include <ESP_I2S.h>
#include <LittleFS.h>
#include <Preferences.h>
#include <esp_task_wdt.h>

#include <atomic>
#include <cmath>
#include <cstring>

#include "cb_config.h"
#include "cb_console.h"
#include "cb_log.h"
#include "cb_wav.h"

using cb::Sound;

namespace {

constexpr uint32_t kDefaultRate = 44100;
constexpr uint32_t kLeadInMs = 20;   // silence after SD_MODE high: amplifier start-up (draft, see Testprotokoll)
constexpr uint32_t kTailMs = 30;     // silence before SD_MODE low: lets the DMA buffer play out completely
constexpr size_t kBlockSamples = 256;  // mono samples per block = 512 bytes read from flash
constexpr uint8_t kDefaultVolume = 75;

struct Request {
  Sound sound;
  uint16_t tone_hz;  // > 0: pure sine instead of a file
  uint16_t tone_ms;
};

I2SClass i2s(I2S_NUM_1);  // I2S1 fixed: PDM microphone input exists only on I2S0 (Softwarearchitektur 2.2)
QueueHandle_t queue = nullptr;
bool fs_ok = false, i2s_ok = false;
uint32_t current_rate = 0;
std::atomic<uint8_t> volume{kDefaultVolume};
std::atomic<bool> playing{false};
std::atomic<uint8_t> playing_sound{0};
uint32_t played = 0, failed = 0, preempted = 0, dropped = 0;
char last_problem[64] = "";
char last_file[24] = "-";
// Held by the audio task while it plays a request; cb_audio_install() takes it to swap a file. LittleFS
// refuses to rename onto an open file, and a tone must never be cut in half by an upload.
SemaphoreHandle_t fs_lock = nullptr;

// What the status page shows about the tone files. Filled at start and after every upload - the page never
// reads the flash itself, so it cannot collide with a tone that is playing.
struct FileInfo {
  bool present;
  cb::WavError err;
  uint32_t bytes, rate, data_len;
  uint8_t channels;
};
FileInfo file_info[(int)Sound::Count];
uint32_t fs_used_cached = 0, fs_total_cached = 0;  // usedBytes() walks the whole file system: ~20 ms
portMUX_TYPE info_lock = portMUX_INITIALIZER_UNLOCKED;

// LittleFS file as cb::ByteSource for the parser.
struct FileSource : cb::ByteSource {
  File &f;
  explicit FileSource(File &file) : f(file) {}
  uint32_t size() override { return f.size(); }
  size_t read_at(uint32_t off, uint8_t *buf, size_t n) override {
    if (!f.seek(off)) return 0;
    return f.read(buf, n);
  }
};

void write_silence(uint32_t ms) {
  static int16_t zero[2 * 128] = {};
  uint32_t frames = current_rate * ms / 1000;
  while (frames) {
    uint32_t k = frames > 128 ? 128 : frames;
    i2s.write((uint8_t *)zero, k * 4);
    frames -= k;
  }
}

bool set_rate(uint32_t rate) {
  if (rate == current_rate) return true;
  if (!i2s.configureTX(rate, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO)) return false;
  current_rate = rate;
  return true;
}

// Checks the queue between two blocks. Returns true if the running tone must stop because `next` replaces it.
bool newer_request(Sound current, Request *next) {
  Request r;
  while (xQueueReceive(queue, &r, 0) == pdTRUE) {
    if (cb::sound_preempts(r.sound, true, current)) {
      *next = r;
      return true;
    }
    dropped++;  // lower priority than what is playing: dropped, as designed
  }
  return false;
}

void amp(bool on) {
  digitalWrite(CB_PIN_AMP_SD_MODE, on ? HIGH : LOW);
}

// Plays one request; returns true if it was replaced by `next` (which must then be played).
bool play_one(const Request &req, Request *next) {
  static int16_t in[kBlockSamples];
  static int16_t out[2 * kBlockSamples];
  playing_sound = (uint8_t)req.sound;

  if (req.tone_hz) {  // pure sine with a 2*pi-wrapped phase accumulator (Softwarearchitektur 9)
    if (!set_rate(kDefaultRate)) return false;
    amp(true);
    write_silence(kLeadInMs);
    uint32_t total = current_rate * req.tone_ms / 1000;
    float phase = 0, step = 2.0f * (float)M_PI * req.tone_hz / current_rate;
    while (total) {
      esp_task_wdt_reset();
      if (newer_request(req.sound, next)) return true;
      size_t n = total > kBlockSamples ? kBlockSamples : total;
      for (size_t i = 0; i < n; i++) {
        in[i] = (int16_t)(32767.0f * sinf(phase));
        phase += step;
        if (phase >= 2.0f * (float)M_PI) phase -= 2.0f * (float)M_PI;
      }
      cb::scale_to_stereo(in, n, true, cb::volume_gain_q15(volume), out);
      i2s.write((uint8_t *)out, n * 4);
      total -= n;
    }
    return false;
  }

  const char *path = cb::sound_file(req.sound);
  snprintf(last_file, sizeof(last_file), "%s", path);
  if (!fs_ok) {
    snprintf(last_problem, sizeof(last_problem), "file system not mounted");
    return false;
  }
  File f = LittleFS.open(path, "r");
  if (!f) {
    snprintf(last_problem, sizeof(last_problem), "%s missing", path);
    return false;
  }
  FileSource src(f);
  cb::WavInfo w{};
  cb::WavError e = cb::parse_wav(src, &w);
  if (e != cb::WavError::Ok) {
    snprintf(last_problem, sizeof(last_problem), "%s: %s", path, cb::wav_error_name(e));
    return false;
  }
  if (!set_rate(w.sample_rate)) {
    snprintf(last_problem, sizeof(last_problem), "%s: I2S rate %lu refused", path, (unsigned long)w.sample_rate);
    return false;
  }
  amp(true);
  write_silence(kLeadInMs);
  bool mono = w.channels == 1;
  uint32_t pos = w.data_offset, end = w.data_offset + w.data_len;
  f.seek(pos);
  while (pos < end) {
    esp_task_wdt_reset();
    if (newer_request(req.sound, next)) return true;
    size_t want = end - pos > sizeof(in) ? sizeof(in) : end - pos;
    size_t got = f.read((uint8_t *)in, want) & ~(size_t)1;
    if (!got) break;  // read error: end the tone quietly
    size_t n = got / 2;
    cb::scale_to_stereo(in, n, mono, cb::volume_gain_q15(volume), out);
    i2s.write((uint8_t *)out, (mono ? n * 2 : n) * 2);
    pos += got;
  }
  return false;
}

void audio_task(void *) {
  esp_task_wdt_add(nullptr);  // supervised like everything else (Softwarearchitektur 3.2, rule 4)
  Request req;
  for (;;) {
    esp_task_wdt_reset();
    if (xQueueReceive(queue, &req, pdMS_TO_TICKS(1000)) != pdTRUE) continue;
    playing = true;
    last_problem[0] = 0;
    Request next;
    bool again;
    xSemaphoreTake(fs_lock, portMAX_DELAY);  // held only briefly by an upload (a rename)
    do {
      again = play_one(req, &next);
      if (again) {
        preempted++;
        req = next;
      }
    } while (again);
    xSemaphoreGive(fs_lock);
    if (last_problem[0]) {
      failed++;
      cb_log("audio: %s - staying silent", last_problem);
    } else {
      played++;
    }
    write_silence(kTailMs);
    amp(false);
    playing = false;
  }
}

void refresh_file_info(Sound s) {
  FileInfo fi{};
  const char *path = cb::sound_file(s);
  File f = fs_ok ? LittleFS.open(path, "r") : File();
  if (f) {
    fi.present = true;
    fi.bytes = f.size();
    FileSource src(f);
    cb::WavInfo w{};
    fi.err = cb::parse_wav(src, &w);
    fi.rate = w.sample_rate;
    fi.channels = w.channels;
    fi.data_len = w.data_len;
  }
  uint32_t used = fs_ok ? LittleFS.usedBytes() : 0, total = fs_ok ? LittleFS.totalBytes() : 0;
  taskENTER_CRITICAL(&info_lock);
  file_info[(int)s] = fi;
  fs_used_cached = used;
  fs_total_cached = total;
  taskEXIT_CRITICAL(&info_lock);
}

void cmd_play(const char *args) {
  bool ok;
  Sound s = cb_audio_parse_sound(args, &ok);
  int hz = atoi(args);
  if (ok) {
    cb_audio_play(s);
    cb_log("play %s (%s) at volume %u %%", cb::sound_name(s), cb::sound_file(s), (unsigned)volume.load());
  } else if (hz >= 100 && hz <= 12000) {
    cb_audio_tone((uint16_t)hz, 1000);
    cb_log("play sine %d Hz, 1 s, volume %u %%", hz, (unsigned)volume.load());
  } else {
    cb_log("usage: play ring|ok|error|nonet|tick|<100..12000 Hz>");
  }
}

void save_volume() {
  Preferences p;
  if (p.begin("cb_set", false)) {
    p.putUChar("vol", volume);
    p.end();
  }
}

void cmd_vol(const char *args) {
  if (!strcmp(args, "next")) {
    cb_audio_volume_step();
  } else if (*args) {
    int v = atoi(args);
    if (!cb::volume_valid((uint8_t)v)) {
      cb_log("vol: allowed are 15, 30, 50, 75, 100");
      return;
    }
    volume = (uint8_t)v;
    save_volume();
  }
  cb_log("volume %u %% (= %d %% digital; 100 %% = %d %% digital, above that the amplifier clips)",
         (unsigned)volume.load(), volume.load() * cb::kMaxDigitalPercent / 100, cb::kMaxDigitalPercent);
}

void cmd_files(const char *) {
  if (!fs_ok) {
    cb_log("files: LittleFS not mounted");
    return;
  }
  cb_log("files: LittleFS %lu of %lu bytes used", (unsigned long)LittleFS.usedBytes(),
         (unsigned long)LittleFS.totalBytes());
  for (int i = 0; i < (int)Sound::Count; i++) {
    const char *path = cb::sound_file((Sound)i);
    File f = LittleFS.open(path, "r");
    if (!f) {
      cb_log("  %-15s MISSING", path);
      continue;
    }
    FileSource src(f);
    cb::WavInfo w{};
    cb::WavError e = cb::parse_wav(src, &w);
    if (e == cb::WavError::Ok)
      cb_log("  %-15s %6lu B, %lu Hz, %s, %.2f s", path, (unsigned long)f.size(), (unsigned long)w.sample_rate,
             w.channels == 1 ? "mono" : "stereo", (float)w.data_len / (2 * w.channels) / w.sample_rate);
    else
      cb_log("  %-15s BROKEN: %s", path, cb::wav_error_name(e));
  }
  cb_log("audio: played %lu, failed %lu, replaced %lu, dropped (lower priority) %lu", (unsigned long)played,
         (unsigned long)failed, (unsigned long)preempted, (unsigned long)dropped);
}

}  // namespace

void cb_audio_begin() {
  // Boot step 8: file system. Never formatted automatically - a damaged FS must not silently erase the tones.
  fs_ok = LittleFS.begin(false, "/littlefs", 4, "littlefs");
  if (!fs_ok) cb_log("audio: LittleFS not mounted - unit stays silent (flash tones with: cb.py toene <port>)");

  Preferences p;
  if (p.begin("cb_set", true)) {
    uint8_t v = p.getUChar("vol", kDefaultVolume);
    volume = cb::volume_valid(v) ? v : kDefaultVolume;
    p.end();
  }

  // Boot step 9: I2S1 to the amplifier, no output yet (SD_MODE stays low).
  i2s.setPins(CB_PIN_I2S_BCLK, CB_PIN_I2S_WS, CB_PIN_I2S_DOUT);
  i2s_ok = i2s.begin(I2S_MODE_STD, kDefaultRate, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO);
  if (!i2s_ok) {
    cb_log("audio: I2S1 not started - unit stays silent");
  } else {
    current_rate = kDefaultRate;
    queue = xQueueCreate(4, sizeof(Request));
    fs_lock = xSemaphoreCreateMutex();
    // Core 1 next to loop(), higher priority: i2s.write() blocks in DMA waits, so it yields constantly.
    xTaskCreatePinnedToCore(audio_task, "audio", 4096, nullptr, 3, nullptr, 1);
  }
  for (int i = 0; i < (int)Sound::Count; i++) refresh_file_info((Sound)i);
  cb_console_add("play", "play ring|ok|error|nonet|tick|<Hz> - play a tone", cmd_play);
  cb_console_add("vol", "vol [next|15|30|50|75|100] - show/set volume", cmd_vol);
  cb_console_add("files", "tone files on LittleFS and audio counters", cmd_files);
  cb_log("audio: %s, volume %u %%",
         !i2s_ok ? "NOT ready (no I2S)" : fs_ok ? "ready" : "amplifier ready, but no tone files (silent)",
         (unsigned)volume.load());
}

void cb_audio_play(Sound s) {
  if (!queue) return;
  Request r{s, 0, 0};
  if (xQueueSend(queue, &r, 0) != pdTRUE) dropped++;
}

void cb_audio_tone(uint16_t hz, uint16_t ms) {
  if (!queue) return;
  Request r{Sound::Error, hz, ms};  // rank of Error: diagnosis tones win over ticks and confirmations
  if (xQueueSend(queue, &r, 0) != pdTRUE) dropped++;
}

void cb_audio_volume_step() {
  volume = cb::volume_next(volume);
  save_volume();
  cb_log("volume %u %%", (unsigned)volume.load());
  cb_audio_play(Sound::Feedback);
}

bool cb_audio_set_volume(uint8_t v) {
  if (!cb::volume_valid(v)) return false;
  volume = v;
  save_volume();
  cb_log("volume %u %% (status page)", (unsigned)v);
  return true;
}

uint8_t cb_audio_volume() {
  return volume;
}

bool cb_audio_busy() {
  return playing;
}

bool cb_audio_fs_ok() {
  return fs_ok;
}

Sound cb_audio_parse_sound(const char *a, bool *known) {
  *known = true;
  if (!strcmp(a, "ring")) return Sound::Ring;
  if (!strcmp(a, "ok")) return Sound::Success;
  if (!strcmp(a, "error")) return Sound::Error;
  if (!strcmp(a, "nonet")) return Sound::NoNetwork;
  if (!strcmp(a, "tick")) return Sound::Feedback;
  *known = false;
  return Sound::Feedback;
}

bool cb_audio_install(const char *tmp_path, Sound target, char *msg, size_t msg_len) {
  if (!fs_ok || !fs_lock) {
    snprintf(msg, msg_len, "Audio nicht bereit");
    return false;
  }
  cb::WavInfo w{};
  {
    File f = LittleFS.open(tmp_path, "r");
    if (!f) {
      snprintf(msg, msg_len, "Zwischendatei fehlt");
      return false;
    }
    FileSource src(f);
    cb::WavError e = cb::parse_wav(src, &w);
    if (e != cb::WavError::Ok) {
      // Same parser as playback: what passes here, plays.
      snprintf(msg, msg_len, "keine 16-Bit-PCM-WAV (%s) - alter Ton bleibt", cb::wav_error_name(e));
      return false;
    }
  }  // file closed before the rename
  const char *dest = cb::sound_file(target);
  if (xSemaphoreTake(fs_lock, pdMS_TO_TICKS(6000)) != pdTRUE) {  // a ring tone lasts < 4 s
    snprintf(msg, msg_len, "Ton spielt gerade - bitte nochmal");
    return false;
  }
  bool ok = LittleFS.rename(tmp_path, dest);  // LittleFS replaces the old file atomically
  xSemaphoreGive(fs_lock);
  refresh_file_info(target);
  if (!ok) {
    snprintf(msg, msg_len, "Umbenennen fehlgeschlagen - alter Ton bleibt");
    return false;
  }
  float sec = (float)w.data_len / (2 * w.channels) / w.sample_rate;
  snprintf(msg, msg_len, "%s ersetzt: %lu Hz, %s, %.2f s", dest, (unsigned long)w.sample_rate,
           w.channels == 1 ? "mono" : "stereo", sec);
  cb_log("audio: %s", msg);
  return true;
}

void cb_audio_json(cb::Json &j) {
  j.key("audio");
  j.begin_obj();
  j.kv("ready", i2s_ok && fs_ok);
  j.kv("volume", (unsigned)volume.load());
  j.kv("max_digital_percent", (int)cb::kMaxDigitalPercent);
  j.kv("last_file", last_file);
  j.kv("playing", playing.load());
  j.kv("played", (unsigned long)played);
  j.kv("failed", (unsigned long)failed);
  j.kv("fs_used", (unsigned long)fs_used_cached);
  j.kv("fs_total", (unsigned long)fs_total_cached);
  j.key("files");
  j.begin_arr();
  static const char *names[] = {"ring", "error", "nonet", "ok", "tick"};  // order of cb::Sound
  for (int i = 0; i < (int)Sound::Count; i++) {
    FileInfo fi;
    taskENTER_CRITICAL(&info_lock);
    fi = file_info[i];
    taskEXIT_CRITICAL(&info_lock);
    j.begin_obj();
    j.kv("name", names[i]);
    j.kv("file", cb::sound_file((Sound)i));
    j.kv("present", fi.present);
    if (fi.present) {
      j.kv("bytes", (unsigned long)fi.bytes);
      j.kv("status", fi.err == cb::WavError::Ok ? "ok" : cb::wav_error_name(fi.err));
      if (fi.err == cb::WavError::Ok) {
        j.kv("rate", (unsigned long)fi.rate);
        j.kv("channels", (unsigned)fi.channels);
        j.key("seconds");
        j.real((double)fi.data_len / (2 * fi.channels) / fi.rate, 2);
      }
    }
    j.end_obj();
  }
  j.end_arr();
  j.end_obj();
}

void cb_audio_restore_factory_tones() {
  if (!fs_ok) return;
  int restored = 0;
  for (int i = 0; i < (int)Sound::Count; i++) {
    const char *dest = cb::sound_file((Sound)i);
    char src[32];
    snprintf(src, sizeof src, "/werk%s", dest);
    File in = LittleFS.open(src, "r");
    if (!in) continue;  // no factory copy (old tone image): keep what is there
    File out = LittleFS.open("/restore.tmp", "w");
    static uint8_t buf[1024];
    bool ok = out;
    while (ok && in.available()) {
      size_t n = in.read(buf, sizeof buf);
      ok = n > 0 && out.write(buf, n) == n;
    }
    in.close();
    if (out) out.close();
    if (ok && fs_lock && xSemaphoreTake(fs_lock, pdMS_TO_TICKS(6000)) == pdTRUE) {
      ok = LittleFS.rename("/restore.tmp", dest);
      xSemaphoreGive(fs_lock);
    } else {
      ok = false;
    }
    if (!ok) LittleFS.remove("/restore.tmp");
    restored += ok;
    refresh_file_info((Sound)i);
  }
  cb_log("audio: %d factory tone(s) restored", restored);
}
