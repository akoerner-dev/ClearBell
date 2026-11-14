// cb_led.h - the two-colour status LED (red GPIO48 + green GPIO18, both active HIGH).
//
// Meaning (Softwarearchitektur 8.2; blink patterns are draft values, decided in bring-up):
//   off            no power, or firmware not running yet (the LED is dark during boot by hardware)
//   green          ready: WiFi up, peer answers the heartbeat
//   red            fault: no WiFi or peer unreachable
//   amber          ring acknowledged (red + green; mixing depends on the diffuser in the wooden front)
//   green blinking maintenance mode (1 Hz)
//   red/green alternating   setup mode (provisioning missing or factory reset done)
//   red fast blinking       fatal configuration error (e.g. provisioning for the wrong image type)
//   amber fast blinking     an owner-key change waits for a short press of the boot button (R32, max. 60 s)
#pragma once

#include <cstdint>

enum class CbLed {
  Off,
  Green,
  Red,
  Amber,
  MaintenanceBlink,
  SetupAlternate,
  ErrorFast,
  ConfirmBlink,
};

void cb_led_begin();
void cb_led_set(CbLed pattern);
CbLed cb_led_get();
void cb_led_poll();  // from loop(): advances blink patterns
uint8_t cb_led_green_percent();  // brightness balance green vs red (PWM, console "led-balance")
bool cb_led_set_green_percent(uint8_t v);  // status page (maintenance): 5..100, stored; false if out of range
