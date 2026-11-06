// cb_modes.cpp - see cb_modes.h.
//
// Console commands registered here:
//   mode            show the current mode
//   factory-reset   erase the BUYER's data (only in SETUP mode = boot button held 2 x 5 s), then reboot;
//                   the device code from the label stays. Same as holding the button 15 s in setup mode.
//   button-sim      TEST: feed a boot-button gesture (long = 5 s hold, short) into the real mode machine.
//                   USB only - the console needs the cable at the opened unit, i.e. physical presence like
//                   the button itself (same reasoning as door-test and touch-sim).
#include "cb_modes.h"

#include <Arduino.h>
#include <cstring>

#include "cb_audio.h"
#include "cb_config.h"
#include "cb_console.h"
#include "cb_led.h"
#include "cb_log.h"
#include "cb_provisioning.h"
#include "cb_setup.h"

namespace {

cb::ButtonGesture button;
cb::ModeMachine machine;
CbLed normal_led = CbLed::Off;  // what the application wants to show in normal mode
cb::ButtonEvent sim_event = cb::ButtonEvent::None;  // console button-sim, consumed by the next poll

void show_mode_led() {
  switch (machine.mode()) {
    case cb::Mode::Normal: cb_led_set(normal_led); break;
    case cb::Mode::Maintenance: cb_led_set(CbLed::MaintenanceBlink); break;
    case cb::Mode::Setup: cb_led_set(CbLed::SetupAlternate); break;
  }
}

void cmd_mode(const char *) {
  cb_log("mode: %s", cb::mode_name(machine.mode()));
}

void cmd_factory_reset(const char *args) {
  if (machine.mode() != cb::Mode::Setup) {
    cb_log("factory-reset: refused - only in setup mode (hold the boot button 5 s twice)");
    return;
  }
  if (strcmp(args, "JA") != 0) {
    cb_log("factory-reset: erases WiFi, pairing, phones, own tones and settings (the device code stays).");
    cb_log("factory-reset: to confirm type: factory-reset JA");
    return;
  }
  cb_setup_factory_reset();
}

void cmd_button_sim(const char *args) {
  if (!strcmp(args, "long")) sim_event = cb::ButtonEvent::Long;
  else if (!strcmp(args, "short")) sim_event = cb::ButtonEvent::Short;
  else if (!strcmp(args, "reset")) sim_event = cb::ButtonEvent::VeryLong;
  else cb_log("usage: button-sim long|short|reset   (long = held 5 s, reset = held 15 s)");
}

}  // namespace

void cb_modes_begin(bool provisioned) {
  machine.begin(provisioned, millis());
  cb_console_add("mode", "show the operating mode", cmd_mode);
  cb_console_add("factory-reset", "factory-reset JA - erase the buyer's data (setup mode only)", cmd_factory_reset);
  cb_console_add("button-sim", "TEST: button-sim long|short - boot button gesture over USB", cmd_button_sim);
  cb_log("mode: %s%s", cb::mode_name(machine.mode()), provisioned ? "" : " (not provisioned)");
  show_mode_led();
}

void cb_modes_poll() {
  uint32_t now = millis();
  cb::ButtonEvent ev = button.update(digitalRead(CB_PIN_BOOT) == LOW, now);
  if (sim_event != cb::ButtonEvent::None) {
    ev = sim_event;
    sim_event = cb::ButtonEvent::None;
    cb_log("button-sim: %s", ev == cb::ButtonEvent::Long ? "long (5 s)" : ev == cb::ButtonEvent::VeryLong ? "15 s" : "short");
  }
  if (ev == cb::ButtonEvent::Long) cb_log("boot button held %lu s", (unsigned long)(cb::kLongHoldMs / 1000));
  cb::ModeAction a = machine.update(ev, now);
  switch (a) {
    case cb::ModeAction::None: break;
    case cb::ModeAction::VolumeStep:
      cb_audio_volume_step();  // logs, stores in NVS, plays a tick at the new volume
      break;
    case cb::ModeAction::Entered:
    case cb::ModeAction::Left:
      cb_log("mode: %s", cb::mode_name(machine.mode()));
      show_mode_led();
      break;
    case cb::ModeAction::FactoryReset:
      cb_log("boot button held 15 s in setup mode: FACTORY RESET");
      cb_setup_factory_reset();
      break;
  }
}

cb::Mode cb_mode() {
  return machine.mode();
}

void cb_modes_set_normal_led(CbLed pattern) {
  if (pattern == normal_led) return;
  normal_led = pattern;
  if (machine.mode() == cb::Mode::Normal) cb_led_set(pattern);
}

void cb_modes_activity() {
  machine.touch_activity(millis());
}

bool cb_modes_request_maintenance() {
  bool ok = machine.request_maintenance(millis());
  if (ok) show_mode_led();
  return ok;
}
