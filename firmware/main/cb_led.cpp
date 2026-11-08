// cb_led.cpp - see cb_led.h.
//
// Both channels run on LEDC PWM (5 kHz, 8 bit - far above any visible flicker). Red at full duty, green
// dimmed: at almost the same current (1.87 mA green / 2.07 mA red) green looks clearly brighter, because
// 570 nm is nearer to the eye's sensitivity maximum (Softwarearchitektur 8.2). The green duty is a draft
// value, set by eye in the hand checks (console "led-balance", stored in NVS "cb_set"/"led_g").
#include "cb_led.h"

#include <Arduino.h>
#include <Preferences.h>

#include "cb_config.h"
#include "cb_console.h"
#include "cb_log.h"

namespace {

constexpr uint32_t kPwmHz = 5000;
constexpr uint8_t kPwmBits = 8;
constexpr uint8_t kRedDuty = 255;
constexpr uint8_t kGreenPercentDefault = 40;  // draft until the hand check with the diffuser

CbLed current = CbLed::Off;
uint8_t green_percent = kGreenPercentDefault;
bool pwm_ok = false;
CbLed shown = CbLed::Off;  // last pattern actually written (avoid rewriting the PWM 1000x per second)
int last_phase = -1;

void out(bool red, bool green) {
  if (pwm_ok) {
    ledcWrite(CB_PIN_LED_RED, red ? kRedDuty : 0);
    ledcWrite(CB_PIN_LED_GRN, green ? (uint32_t)green_percent * 255 / 100 : 0);
  } else {  // fallback without PWM: plain on/off, the LED still says what it must
    digitalWrite(CB_PIN_LED_RED, red ? HIGH : LOW);
    digitalWrite(CB_PIN_LED_GRN, green ? HIGH : LOW);
  }
}

bool set_green(int v) {
  if (v < 5 || v > 100) return false;
  green_percent = (uint8_t)v;
  Preferences p;
  if (p.begin("cb_set", false)) {
    p.putUChar("led_g", green_percent);
    p.end();
  }
  last_phase = -1;  // force a rewrite
  return true;
}

void cmd_led_balance(const char *args) {
  if (*args && !set_green(atoi(args))) {
    cb_log("usage: led-balance [5..100]  - green brightness in %% of red (default %u)", kGreenPercentDefault);
    return;
  }
  cb_log("led: green at %u %% of red (PWM %lu Hz)%s", green_percent, (unsigned long)kPwmHz,
         pwm_ok ? "" : " - PWM NOT available, plain on/off");
}

}  // namespace

void cb_led_begin() {
  // Pins low first (dark = "firmware not running yet"), then hand them to the PWM unit.
  pinMode(CB_PIN_LED_RED, OUTPUT);
  pinMode(CB_PIN_LED_GRN, OUTPUT);
  digitalWrite(CB_PIN_LED_RED, LOW);
  digitalWrite(CB_PIN_LED_GRN, LOW);
  pwm_ok = ledcAttach(CB_PIN_LED_RED, kPwmHz, kPwmBits) && ledcAttach(CB_PIN_LED_GRN, kPwmHz, kPwmBits);
  out(false, false);
  Preferences p;
  if (p.begin("cb_set", true)) {
    uint8_t v = p.getUChar("led_g", kGreenPercentDefault);
    green_percent = v >= 5 && v <= 100 ? v : kGreenPercentDefault;
    p.end();
  }
  cb_console_add("led-balance", "led-balance [5..100] - green brightness relative to red", cmd_led_balance);
}

void cb_led_set(CbLed pattern) {
  current = pattern;
  cb_led_poll();
}

CbLed cb_led_get() {
  return current;
}

uint8_t cb_led_green_percent() {
  return green_percent;
}

bool cb_led_set_green_percent(uint8_t v) {
  if (!set_green(v)) return false;
  cb_log("led: green at %u %% of red (status page)", (unsigned)v);
  return true;
}

void cb_led_poll() {
  uint32_t t = millis();
  // phase: everything the output depends on, so the PWM is written only when something changes
  int phase = 0;
  switch (current) {
    case CbLed::MaintenanceBlink:
    case CbLed::SetupAlternate: phase = (t / 500) % 2; break;
    case CbLed::ErrorFast: phase = (t / 100) % 2; break;
    default: break;
  }
  if (current == shown && phase == last_phase) return;
  shown = current;
  last_phase = phase;
  switch (current) {
    case CbLed::Off: out(false, false); break;
    case CbLed::Green: out(false, true); break;
    case CbLed::Red: out(true, false); break;
    case CbLed::Amber: out(true, true); break;
    case CbLed::MaintenanceBlink: out(false, phase == 0); break;
    case CbLed::SetupAlternate: out(phase == 0, phase != 0); break;
    case CbLed::ErrorFast: out(phase == 0, false); break;
  }
}
