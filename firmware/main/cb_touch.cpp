// cb_touch.cpp - see cb_touch.h.
#include "cb_touch.h"

#include <Arduino.h>

#include <cstring>

#include "cb_audio.h"
#include "cb_config.h"
#include "cb_console.h"
#include "cb_log.h"
#include "cb_modes.h"
#include "cb_net.h"
#include "cb_touch_logic.h"

namespace {

constexpr uint32_t kSampleMs = 20;

uint32_t next_sample = 0, read_us_max = 0, read_us_sum = 0, read_n = 0;
uint32_t log_until = 0, next_log = 0;

// Simulation (console touch-sim): replaces the raw value of one channel for a while.
struct Sim {
  int channel = -1;  // 0 = EG / indoor key, 1 = OG, 2 = both (outdoor)
  float percent = 0;
  uint32_t until = 0;
} sim;

#if CB_IS_OUTDOOR
cb::BellKeys keys;
const cb::TouchChannel &chan(int i) { return i == 0 ? keys.eg() : keys.og(); }
constexpr int kChannels = 2;
const char *kName[2] = {"EG", "OG"};
#else
cb::DoorKey key;
const cb::TouchChannel &chan(int) { return key.channel(); }
constexpr int kChannels = 1;
const char *kName[1] = {"TOUCH"};
#endif

uint32_t sample(int i, uint32_t pin, uint32_t now) {
  uint32_t t0 = micros();
  uint32_t raw = touchRead(pin);
  uint32_t dt = micros() - t0;
  if (dt > read_us_max) read_us_max = dt;
  read_us_sum += dt;
  read_n++;
  if ((sim.channel == i || sim.channel == 2) && (int32_t)(sim.until - now) > 0 && chan(i).ready())
    raw = (uint32_t)(chan(i).baseline() * (1 + sim.percent / 100));
  return raw;
}

void log_line(uint32_t now) {
  for (int i = 0; i < kChannels; i++) {
    const cb::TouchChannel &c = chan(i);
    cb_log("touch %-5s raw %6lu base %8.0f delta %+7.1f %% %s%s", kName[i], (unsigned long)c.raw(), c.baseline(),
           c.delta_percent(), c.pressed() ? "PRESSED" : c.ready() ? "rest" : "learning",
           sim.channel == i && (int32_t)(sim.until - now) > 0 ? " (sim)" : "");
  }
}

void cmd_touch(const char *) {
  log_line(millis());
#if CB_IS_OUTDOOR
  cb_log("touch: rejected (both keys at once) %lu, relearned EG %lu / OG %lu, slowest read %lu us",
         (unsigned long)keys.rejected_count(), (unsigned long)keys.eg().relearn_count(),
         (unsigned long)keys.og().relearn_count(), (unsigned long)read_us_max);
#else
  cb_log("touch: window %s, rejected %lu, relearned %lu, slowest read %lu us",
         key.armed(millis()) ? "ARMED (tap opens)" : "closed (hold 3 s)", (unsigned long)key.rejected_count(),
         (unsigned long)key.channel().relearn_count(), (unsigned long)read_us_max);
#endif
  cb_log("touch: touchRead() since last query: %lu calls, mean %lu us, max %lu us", (unsigned long)read_n,
         (unsigned long)(read_n ? read_us_sum / read_n : 0), (unsigned long)read_us_max);
  read_us_sum = read_n = read_us_max = 0;
  cb_log("touch: thresholds on +%.0f %% / off +%.0f %% (drafts until the mounted unit)", cb::kOnPercent,
         cb::kOffPercent);
}

void cmd_touch_log(const char *args) {
  int s = atoi(args);
  if (s < 1 || s > 120) s = 10;
  log_until = millis() + s * 1000;
  next_log = millis();
}

void cmd_touch_relearn(const char *) {
  // TouchChannel objects are owned by this module; relearn through a fresh object keeps it simple and safe.
#if CB_IS_OUTDOOR
  keys = cb::BellKeys();
#else
  bool armed = key.armed(millis());
  key = cb::DoorKey();
  if (armed) key.arm(millis());
#endif
  cb_log("touch: learning the baseline - do not touch for 1 s");
}

void cmd_touch_sim(const char *args) {
  int ch = 0;
  const char *p = args;
#if CB_IS_OUTDOOR
  if (!strncmp(p, "eg ", 3)) ch = 0;
  else if (!strncmp(p, "og ", 3)) ch = 1;
  else if (!strncmp(p, "both ", 5)) {
    ch = 2;  // both keys at once: the wetting case of the plausibility rule
    p += 2;
  }
  else {
    cb_log("usage: touch-sim eg|og|both <percent> <ms>   e.g. touch-sim eg 400 300");
    return;
  }
  p += 3;
#endif
  float pct = atof(p);
  const char *q = strchr(p, ' ');
  int ms = q ? atoi(q + 1) : 0;
  if (ms <= 0 || ms > 30000) {
    cb_log("usage: touch-sim %s<percent> <ms>", CB_IS_OUTDOOR ? "eg|og " : "");
    return;
  }
  sim.channel = ch;
  sim.percent = pct;
  sim.until = millis() + ms;
  cb_log("touch-sim %s: +%.0f %% for %d ms", ch == 2 ? "BOTH" : kName[ch], pct, ms);
}

}  // namespace

void cb_touch_begin() {
  cb_console_add("touch", "touch values, state, counters", cmd_touch);
  cb_console_add("touch-log", "touch-log <s> - values every 100 ms", cmd_touch_log);
  cb_console_add("touch-relearn", "learn the touch baseline anew", cmd_touch_relearn);
  cb_console_add("touch-sim", "TEST: simulate a touch (percent above baseline, duration)", cmd_touch_sim);
  cb_log("touch: learning the baseline - keep the keys untouched for 1 s");
}

void cb_touch_poll() {
  uint32_t now = millis();
  if ((int32_t)(now - next_sample) < 0) return;
  next_sample = now + kSampleMs;
  bool active = cb_mode() != cb::Mode::Setup;  // in setup mode the keys do nothing

#if CB_IS_OUTDOOR
  uint32_t eg = sample(0, CB_PIN_TOUCH_EG, now), og = sample(1, CB_PIN_TOUCH_OG, now);
  bool was_ready = keys.eg().ready() && keys.og().ready();
  cb::BellKeys::Out o = keys.update(eg, og, now);
  if (!was_ready && keys.eg().ready() && keys.og().ready())
    cb_log("touch: baseline EG %.0f, OG %.0f", keys.eg().baseline(), keys.og().baseline());
  if (o == cb::BellKeys::Out::RingEG || o == cb::BellKeys::Out::RingOG) {
    cb::UnitId flat = o == cb::BellKeys::Out::RingEG ? cb::UnitId::EG : cb::UnitId::OG;
    cb_log("touch: bell %s pressed", cb::unit_name(flat));
    if (active) cb_net_ring(flat);  // result tone: cb_on_ring_result() in main.cpp
  } else if (o == cb::BellKeys::Out::Rejected) {
    cb_log("touch: BOTH keys raised at once - wetting, no ring");
  }
#else
  uint32_t raw = sample(0, CB_PIN_TOUCH, now);
  bool was_ready = key.channel().ready();
  cb::DoorKey::Out o = key.update(raw, now);
  if (!was_ready && key.channel().ready()) cb_log("touch: baseline %.0f", key.channel().baseline());
  switch (o) {
    case cb::DoorKey::Out::Open:
      cb_log("touch: door key released - %s", key.armed(now) ? "tap in the armed window" : "after the 3-s hold");
      if (active) cb_net_door_request();  // result tone: cb_on_door_result() in main.cpp
      break;
    case cb::DoorKey::Out::ReleaseNow:
      cb_log("touch: held 3 s - release now to open");
      cb_audio_play(cb::Sound::Feedback);
      break;
    case cb::DoorKey::Out::Rejected:
      cb_log("touch: door key ignored (too short/long, outside the window, or something lying on it)");
      break;
    case cb::DoorKey::Out::None:
      break;
  }
#endif

  if (log_until && (int32_t)(log_until - now) > 0 && (int32_t)(now - next_log) >= 0) {
    next_log = now + 100;
    log_line(now);
  } else if (log_until && (int32_t)(log_until - now) <= 0) {
    log_until = 0;
  }
}

void cb_touch_arm() {
#if !CB_IS_OUTDOOR
  key.arm(millis());
  cb_log("touch: window armed for %lu s - a short tap opens", (unsigned long)(cb::kArmedWindowMs / 1000));
#endif
}

void cb_touch_disarm() {
#if !CB_IS_OUTDOOR
  key.disarm();
#endif
}

void cb_touch_json(cb::Json &j) {
  uint32_t now = millis();
  j.key("touch");
  j.begin_obj();
  j.key("channels");
  j.begin_arr();
  for (int i = 0; i < kChannels; i++) {
    const cb::TouchChannel &c = chan(i);
    j.begin_obj();
    j.kv("name", kName[i]);
    j.kv("raw", (unsigned long)c.raw());
    j.key("baseline");
    j.real(c.baseline(), 0);
    j.key("delta_percent");
    j.real(c.delta_percent(), 1);
    j.kv("state", c.pressed() ? "pressed" : c.ready() ? "rest" : "learning");
    j.kv("relearned", (unsigned long)c.relearn_count());
    j.end_obj();
  }
  j.end_arr();
#if CB_IS_OUTDOOR
  j.kv("rejected_both", (unsigned long)keys.rejected_count());
#else
  j.kv("armed", key.armed(now));
  j.kv("rejected", (unsigned long)key.rejected_count());
#endif
  j.key("on_percent");
  j.real(cb::kOnPercent, 0);
  j.key("off_percent");
  j.real(cb::kOffPercent, 0);
  j.kv("simulated", sim.channel >= 0 && (int32_t)(sim.until - now) > 0);
  j.end_obj();
}
