// main.cpp - ClearBell V0.2 firmware entry point: start sequence (setup) and main loop.
//
// Read this file first. It shows WHAT happens in which order; the modules (cb_*.cpp) show HOW.
// The order of setup() is the safety-critical start sequence of Softwarearchitektur 5.1:
// first bring everything into a safe state, then switch functions on.
//
// Main loop rules (Softwarearchitektur 3.2): loop() never blocks, every pass yields, everything is
// supervised by the task watchdog (5 s, panic -> reboot).
#include <Arduino.h>
#include <driver/gpio.h>
#include <esp_ota_ops.h>
#include <esp_system.h>
#include <esp_task_wdt.h>

#include "cb_audio.h"
#include "cb_config.h"
#include "cb_console.h"
#include "cb_door.h"
#include "cb_door_logic.h"
#include "cb_mic.h"
#include "cb_led.h"
#include "cb_log.h"
#include "cb_modes.h"
#include "cb_net.h"
#include "cb_provisioning.h"
#include "cb_remote.h"
#include "cb_status.h"
#include "cb_system.h"
#include "cb_touch.h"
#include "cb_update.h"
#include "cb_setup.h"
#include "cb_pairing.h"
#include "cb_phones.h"

// ---- Boot step 1, as early as at all possible ----------------------------------------------------------
// GPIO47 (door opener gate) is high-impedance after reset; until software drives it, only the 100 kOhm
// pull-down at the MOSFET gate keeps the door shut. This constructor runs during C++ static
// initialisation - before Arduino's own start-up and long before setup() - so the window in which the
// pull-down alone holds the gate is as short as the chip allows. setup() repeats it (belt and braces).
// The amplifier is muted in the same breath (no power-on pop).
__attribute__((constructor(101))) static void cb_safe_state_early() {
#if CB_IS_OUTDOOR
  gpio_set_level((gpio_num_t)CB_PIN_DOOR_DRV, 0);
  gpio_set_direction((gpio_num_t)CB_PIN_DOOR_DRV, GPIO_MODE_OUTPUT);
  gpio_set_level((gpio_num_t)CB_PIN_DOOR_DRV, 0);
#endif
  gpio_set_level((gpio_num_t)CB_PIN_AMP_SD_MODE, 0);
  gpio_set_direction((gpio_num_t)CB_PIN_AMP_SD_MODE, GPIO_MODE_OUTPUT);
  gpio_set_level((gpio_num_t)CB_PIN_AMP_SD_MODE, 0);
}

// Arduino would confirm a freshly installed OTA image immediately at boot. Returning true here keeps the
// image "pending" until cb_system decides it is healthy (Softwarearchitektur 8.1). Images flashed over USB
// are never pending, so this only matters for updates.
extern "C" bool verifyRollbackLater() {
  return true;
}

void setup() {
  // Step 1: door gate LOW (outdoor). Step 2: amplifier muted.
#if CB_IS_OUTDOOR
  pinMode(CB_PIN_DOOR_DRV, OUTPUT);
  digitalWrite(CB_PIN_DOOR_DRV, LOW);
#endif
  pinMode(CB_PIN_AMP_SD_MODE, OUTPUT);
  digitalWrite(CB_PIN_AMP_SD_MODE, LOW);

  // Step 3: status LED off - "dark" means "firmware not running yet".
  cb_led_begin();
  pinMode(CB_PIN_BOOT, INPUT);  // external 10 kOhm pull-up on the board

  // Step 4: logging over USB, never blocking.
  cb_log_begin();

  // Step 5: reset reason and boot counter; firmware version and partition into the log.
  cb_system_begin();

  // Step 6: factory data (role, keys, WiFi). Missing or for the wrong image -> setup mode, nothing else.
  cb_provisioning_begin();
  // A freshly updated firmware runs on probation (port step 10); a test fault armed with update-fault acts here.
  cb_update_begin();
  cb_modes_begin(cb_prov().valid);

  // Steps 7-9: settings (volume) from NVS, LittleFS, I2S1 to the amplifier - no sound yet.
  cb_audio_begin();
  // Step 10: the microphone is NOT started (privacy by default); only diagnosis commands may switch it on.
  cb_mic_begin();

  // Step 11: touch keys; the baseline is learned during the first calm second (keys untouched).
  cb_touch_begin();

  // Step 12: WiFi and protocol - in the background, the unit works without network too. Not in setup
  // mode: without valid provisioning there is neither WiFi access nor a key.
  if (cb_prov().valid) cb_net_begin();

  // Step 13: door task (outdoor; owns GPIO47 from here on). Tasks remote and the update self-test with peer
  // contact (step 14) follow with steps 9-10.
  cb_door_begin();

  // Remote opening by phone (port step 9, outdoor with provisioned topics only).
  cb_remote_begin();

  // Setup access point (port step 10a): at once when not set up (also starts the network stack), later on demand.
  cb_setup_begin();

  // Status page and door log (port step 8). Last: the page is the least important function (SA 8.1).
  cb_status_begin();

  // The loop task itself is supervised from here on (the Arduino core does not do that by default).
  enableLoopWDT();
  cb_log("ready - type 'help' for the console commands");
}

// ---- Application reactions: tone + LED + log ----------------------------------------------------------

static uint32_t amber_until = 0;  // amber = "ring acknowledged / received", for 2 s

void cb_on_ring_received() {  // indoor: the ack is already out (ack before action), now ring
  cb_log("RING from the front door");
  cb_audio_play(cb::Sound::Ring);
  cb_touch_arm();  // R18: for 2 min a short tap on the door key opens
  amber_until = millis() + 2000;
}

void cb_on_ring_result(cb::UnitId flat, bool ok) {  // outdoor: what the visitor hears
  if (ok) {
    cb_log("ring %s: acknowledged by the flat", cb::unit_name(flat));
    cb_audio_play(cb::Sound::Success);
    amber_until = millis() + 2000;
  } else if (!cb_net_wifi_up()) {
    // F-4: without WiFi a distinguishable hint - a ring confirmation would pretend someone was notified.
    cb_log("ring %s: NO WIFI - nobody was notified", cb::unit_name(flat));
    cb_audio_play(cb::Sound::NoNetwork);
  } else {
    cb_log("ring %s: FAILED - flat did not answer", cb::unit_name(flat));
    cb_audio_play(cb::Sound::Error);
  }
}

#if CB_IS_OUTDOOR
// Door command arrived and the flat proved its key. Decide (the DoorAck carries the answer), then - after the
// ack is out - hand over to the door task, which alone switches GPIO47 (cb_door.h). The lock-out lives there
// and applies to all paths.
cb::DoorResult cb_on_door_decide(cb::UnitId flat) {
  if (cb_mode() == cb::Mode::Setup) return cb::DoorResult::NotAllowed;
  if (!cb_door_allowed()) {
    cb_log("door: request from %s refused - lock-out time running", cb::unit_name(flat));
    return cb::DoorResult::Locked;
  }
  return cb::DoorResult::Opened;
}

void cb_on_door_open(cb::UnitId flat) {
  if (!cb_door_open(cb::unit_name(flat), cb_door_pulse_ms())) {
    // Only possible if another path opened between decision and execution (a few microseconds).
    cb_log("door: opening for %s not executed - lock-out started meanwhile", cb::unit_name(flat));
    return;
  }
  cb_status_door_local();  // SA 6.4: on the outdoor unit openings by indoor keys only as a sum
  amber_until = millis() + 2000;
}
#else
void cb_on_door_result(cb::DoorResult r, bool answered, uint32_t elapsed_ms) {
  static const char *names[] = {"OPENED", "refused: lock-out time", "refused: bad or expired challenge",
                                "refused: not allowed"};
  if (!answered) cb_log("door: NO ANSWER from the outdoor unit after %lu ms", (unsigned long)elapsed_ms);
  else cb_log("door: %s after %lu ms", (unsigned)r < 4 ? names[(int)r] : "?", (unsigned long)elapsed_ms);
  bool opened = answered && r == cb::DoorResult::Opened;
  // Door log of this flat (SA 6.4): every command with its result.
  cb_status_door_event(!answered                            ? cb::DoorEvent::NoAnswer
                       : opened                             ? cb::DoorEvent::Opened
                       : r == cb::DoorResult::Locked        ? cb::DoorEvent::LockedOut
                                                            : cb::DoorEvent::Refused);
  cb_audio_play(opened ? cb::Sound::Success : cb::Sound::Error);  // F-2: tone; the reason is in the log
  if (opened) {
    amber_until = millis() + 2000;
    cb_touch_disarm();  // draft decision: the armed window ends with the first opening
  }
}
#endif

// Events from a peer (port step 10b): maintenance request from an indoor unit.
void cb_on_event(cb::UnitId from, cb::EventCode code, const uint8_t *data, size_t len) {
  cb_pairing_on_event(from, code, data, len);  // maintenance request
  cb_phones_on_event(from, code, data, len);   // phone keys (port step 10c)
}

void cb_on_event_result(cb::UnitId to, cb::EventCode code, bool ok) {
  cb_phones_on_event_result(to, code, ok);
}

// Normal-mode LED (Softwarearchitektur 8.2): green = WiFi up and every peer answers the heartbeat,
// red = no WiFi or a peer unreachable, amber for 2 s after a ring.
static void update_status_led() {
  if (!cb_prov().valid) return;
  CbLed led;
  if ((int32_t)(amber_until - millis()) > 0) {
    led = CbLed::Amber;
  } else {
    bool ok = cb_net_wifi_up();
#if CB_IS_OUTDOOR
    ok = ok && cb_net_peer_reachable(cb::UnitId::EG) && cb_net_peer_reachable(cb::UnitId::OG);
#else
    ok = ok && cb_net_peer_reachable(cb::UnitId::Outdoor);
#endif
    led = ok ? CbLed::Green : CbLed::Red;
  }
  cb_modes_set_normal_led(led);
}

void loop() {
  cb_console_poll();
  if (cb_prov().valid) cb_net_poll();
  cb_touch_poll();
  cb_modes_poll();
  update_status_led();
  cb_led_poll();
  cb_system_poll();
  cb_remote_poll();  // outdoor: phone commands -> door task; indoor: outage notice (R27)
  cb_pairing_poll(); // maintenance request to the outdoor unit (indoor)
  cb_phones_poll();  // phone events to the outdoor unit (indoor)
  cb_setup_poll();   // setup access point on/off, reboot after saving
  cb_update_poll();  // update probation: confirm or roll back; reboot after an upload
  cb_status_poll();  // builds the status snapshot when the web server asks
  // Yield every pass: keeps the idle task of core 1 alive (it is supervised by the watchdog too) and
  // makes loop() a 1 kHz scheduler at most, which is plenty for touch, LED and transport.
  delay(1);
}
