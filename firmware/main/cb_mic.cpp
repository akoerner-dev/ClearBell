// cb_mic.cpp - see cb_mic.h. The measurements run in a short-lived task, so loop() (network, door) keeps
// running while the microphone records.
#include "cb_mic.h"

#include <Arduino.h>
#include <driver/i2s_pdm.h>
#include <esp_heap_caps.h>
#include <esp_task_wdt.h>

#include <cstring>

#include "cb_audio.h"
#include "cb_config.h"
#include "cb_console.h"
#include "cb_dsp.h"
#include "cb_log.h"

namespace {

constexpr uint32_t kRate = 24000;
constexpr float kHeardDb = 10.0f;  // level rise that counts as "the tone is audible"

i2s_chan_handle_t rx = nullptr;
volatile bool busy = false;  // one measurement at a time; only set/cleared by loop() and the measurement task

struct Job {
  bool listen;
  cb::Sound sound;
  uint16_t hz;        // 0 = file tone
  uint16_t seconds;   // listen only
};
Job job;

bool mic_on() {
  i2s_chan_config_t ch = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
  if (i2s_new_channel(&ch, nullptr, &rx) != ESP_OK) return false;
  i2s_pdm_rx_config_t cfg = {};
  cfg.clk_cfg = I2S_PDM_RX_CLK_DEFAULT_CONFIG(kRate);
  cfg.clk_cfg.dn_sample_mode = I2S_PDM_DSR_8S;
  cfg.slot_cfg = I2S_PDM_RX_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO);
  cfg.gpio_cfg.clk = (gpio_num_t)CB_PIN_MIC_CLK;
  cfg.gpio_cfg.din = (gpio_num_t)CB_PIN_MIC_DATA;
  if (i2s_channel_init_pdm_rx_mode(rx, &cfg) != ESP_OK || i2s_channel_enable(rx) != ESP_OK) {
    i2s_del_channel(rx);
    rx = nullptr;
    return false;
  }
  cb_log("microphone ON (diagnosis)");
  return true;
}

void mic_off() {
  if (!rx) return;
  i2s_channel_disable(rx);
  i2s_del_channel(rx);  // releases the clock pin: the microphone is unpowered-idle again
  rx = nullptr;
  cb_log("microphone OFF");
}

void record(int16_t *buf, size_t samples) {
  size_t done = 0;
  while (done < samples) {
    size_t n = 0;
    i2s_channel_read(rx, buf + done, (samples - done) * 2, &n, pdMS_TO_TICKS(200));
    done += n / 2;
    esp_task_wdt_reset();
  }
}

void measure_task(void *) {
  esp_task_wdt_add(nullptr);
  const size_t win = kRate / 4;  // 250 ms
  int16_t *buf = (int16_t *)heap_caps_malloc(win * 2 * 2, MALLOC_CAP_SPIRAM);  // two windows
  if (!buf || !mic_on()) {
    cb_log("mic: could not start (%s)", buf ? "I2S0" : "memory");
  } else {
    record(buf, win);  // discard: the microphone needs a moment after the clock starts (bring-up: 200 ms)
    if (job.listen) {
      for (int i = 0; i < job.seconds * 4; i++) {
        record(buf, win);
        if (job.hz)
          cb_log("listen %5.2f s: %6.1f dBFS, at %u Hz %6.1f dBFS", (i + 1) * 0.25f, cb::rms_dbfs(buf, win), job.hz,
                 cb::tone_dbfs(buf, win, kRate, job.hz));
        else
          cb_log("listen %5.2f s: %6.1f dBFS", (i + 1) * 0.25f, cb::rms_dbfs(buf, win));
      }
    } else {
      record(buf, 2 * win);  // 0.5 s silence
      float quiet = cb::rms_dbfs(buf, 2 * win);
      float quiet_f = job.hz ? cb::tone_dbfs(buf, 2 * win, kRate, job.hz) : 0;
      if (job.hz) cb_audio_tone(job.hz, 1200);
      else cb_audio_play(job.sound);
      for (int i = 0; i < 50 && !cb_audio_busy(); i++) vTaskDelay(pdMS_TO_TICKS(10));
      vTaskDelay(pdMS_TO_TICKS(60));  // skip lead-in silence and the amplifier start
      record(buf, 2 * win);           // 0.5 s while the tone plays
      float loud = cb::rms_dbfs(buf, 2 * win);
      bool heard = loud - quiet >= kHeardDb;
      if (job.hz) {
        float loud_f = cb::tone_dbfs(buf, 2 * win, kRate, job.hz);
        heard = loud_f - quiet_f >= kHeardDb;
        cb_log("audio-check %u Hz at volume %u %%: silence %.1f dBFS (at %u Hz %.1f), tone %.1f dBFS (at %u Hz "
               "%.1f) -> %+.1f dB at the tone frequency: %s",
               job.hz, (unsigned)cb_audio_volume(), quiet, job.hz, quiet_f, loud, job.hz, loud_f, loud_f - quiet_f,
               heard ? "HEARD" : "NOT HEARD");
      } else {
        cb_log("audio-check %s at volume %u %%: silence %.1f dBFS, tone %.1f dBFS -> %+.1f dB: %s",
               cb::sound_name(job.sound), (unsigned)cb_audio_volume(), quiet, loud, loud - quiet,
               heard ? "HEARD" : "NOT HEARD");
      }
    }
  }
  mic_off();
  if (buf) heap_caps_free(buf);
  busy = false;
  esp_task_wdt_delete(nullptr);
  vTaskDelete(nullptr);
}

bool start(const Job &j) {
  if (busy) {
    cb_log("mic: a measurement is already running");
    return false;
  }
  busy = true;
  job = j;
  if (xTaskCreatePinnedToCore(measure_task, "mic", 4096, nullptr, 2, nullptr, 0) != pdPASS) {
    busy = false;
    return false;
  }
  return true;
}

void cmd_audio_check(const char *args) {
  Job j{false, cb::Sound::Ring, 0, 0};
  if (!strcmp(args, "ring")) j.sound = cb::Sound::Ring;
  else if (!strcmp(args, "ok")) j.sound = cb::Sound::Success;
  else if (!strcmp(args, "error")) j.sound = cb::Sound::Error;
  else if (!strcmp(args, "nonet")) j.sound = cb::Sound::NoNetwork;
  else if (!strcmp(args, "tick")) j.sound = cb::Sound::Feedback;
  else if (atoi(args) >= 100 && atoi(args) <= 10000) j.hz = (uint16_t)atoi(args);
  else {
    cb_log("usage: audio-check ring|ok|error|nonet|tick|<100..10000 Hz>");
    return;
  }
  start(j);
}

void cmd_listen(const char *args) {
  int s = atoi(args);
  if (s < 1 || s > 10) {
    cb_log("usage: listen <1..10 seconds> [Hz]");
    return;
  }
  const char *hz = strchr(args, ' ');
  Job j{true, cb::Sound::Ring, (uint16_t)(hz ? atoi(hz + 1) : 0), (uint16_t)s};
  start(j);
}

}  // namespace

void cb_mic_begin() {
  cb_console_add("audio-check", "play a tone and verify it with the own microphone", cmd_audio_check);
  cb_console_add("listen", "listen <s> [Hz] - microphone levels every 250 ms (diagnosis)", cmd_listen);
}
