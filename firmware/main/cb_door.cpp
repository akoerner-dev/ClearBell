// cb_door.cpp - see cb_door.h.
#include "cb_door.h"

#include <Arduino.h>
#include <driver/gpio.h>
#include <driver/gptimer.h>
#include <esp_task_wdt.h>
#include <esp_timer.h>
#include <hal/gpio_ll.h>

#include <cstring>

#include "cb_config.h"
#include "cb_console.h"
#include "cb_door_logic.h"
#include "cb_log.h"
#include "cb_status.h"

#include <Preferences.h>

#if CB_IS_OUTDOOR

namespace {

uint32_t pulse_setting = cb::kDoorPulseMs;

struct Request {
  uint32_t pulse_ms;
  char who[16];
};

QueueHandle_t queue = nullptr;
SemaphoreHandle_t guard_lock = nullptr;  // DoorGuard is asked from loop() and (later) the remote task
SemaphoreHandle_t pulse_done = nullptr;  // given by the timer interrupt
gptimer_handle_t timer = nullptr;
cb::DoorGuard guard;

volatile int64_t isr_end_us = 0;
int64_t last_start_us = 0;
uint32_t last_len_ms = 0, pulse_errors = 0;
char last_who[16] = "-";

// Timer interrupt: ends the pulse. Runs no matter what loop() or the door task are doing.
bool IRAM_ATTR on_alarm(gptimer_handle_t t, const gptimer_alarm_event_data_t *, void *) {
  gpio_ll_set_level(&GPIO, (gpio_num_t)CB_PIN_DOOR_DRV, 0);
  isr_end_us = esp_timer_get_time();
  gptimer_stop(t);
  BaseType_t woken = pdFALSE;
  xSemaphoreGiveFromISR(pulse_done, &woken);
  return woken == pdTRUE;
}

void pulse(const Request &r) {
  uint32_t ms = cb::DoorGuard::clamp_pulse(r.pulse_ms);
  gptimer_alarm_config_t alarm = {};
  alarm.alarm_count = (uint64_t)ms * 1000;  // timer runs at 1 MHz
  gptimer_set_raw_count(timer, 0);
  gptimer_set_alarm_action(timer, &alarm);
  xSemaphoreTake(pulse_done, 0);  // clear a stale signal

  last_start_us = esp_timer_get_time();
  gpio_set_level((gpio_num_t)CB_PIN_DOOR_DRV, 1);  // ---- door opener ON ----
  gptimer_start(timer);
  bool high_ok = gpio_get_level((gpio_num_t)CB_PIN_DOOR_DRV) == 1;
  cb_log("DOOR: pulse %lu ms for %s, pin read back %s", (unsigned long)ms, r.who, high_ok ? "HIGH" : "NOT HIGH (!)");

  // Wait for the interrupt, feeding our own watchdog meanwhile. Upper bound pulse + 500 ms.
  bool ended = false;
  for (uint32_t waited = 0; waited < ms + 500 && !ended; waited += 100) {
    ended = xSemaphoreTake(pulse_done, pdMS_TO_TICKS(100)) == pdTRUE;
    esp_task_wdt_reset();
  }
  if (!ended || gpio_get_level((gpio_num_t)CB_PIN_DOOR_DRV) != 0) {
    // Should never happen: the timer did not end the pulse. Force it off and report.
    gpio_set_level((gpio_num_t)CB_PIN_DOOR_DRV, 0);
    gptimer_stop(timer);
    pulse_errors++;
    cb_log("DOOR: ERROR - timer did not end the pulse, forced LOW");
  }
  last_len_ms = ended ? (uint32_t)((isr_end_us - last_start_us + 500) / 1000) : ms + 500;
  strncpy(last_who, r.who, sizeof(last_who) - 1);
  cb_log("DOOR: pulse ended after %lu ms, pin read back %s", (unsigned long)last_len_ms,
         gpio_get_level((gpio_num_t)CB_PIN_DOOR_DRV) == 0 ? "LOW" : "STILL HIGH (!)");
}

void door_task(void *) {
  esp_task_wdt_add(nullptr);
  Request r;
  for (;;) {
    esp_task_wdt_reset();
    if (xQueueReceive(queue, &r, pdMS_TO_TICKS(1000)) == pdTRUE) pulse(r);
  }
}

void cmd_door_test(const char *args) {
  uint32_t ms = *args ? (uint32_t)atoi(args) : pulse_setting;
  if (ms < cb::kDoorPulseMinMs || ms > cb::kDoorPulseMaxMs) {
    cb_log("usage: door-test [%lu..%lu ms]", (unsigned long)cb::kDoorPulseMinMs, (unsigned long)cb::kDoorPulseMaxMs);
    return;
  }
  if (!cb_door_open("console", ms)) cb_log("door-test: refused - lock-out time running");
  else cb_status_door_event(cb::DoorEvent::Service);  // service pulses are logged one by one (SA 6.4)
}

void cmd_door_status(const char *) {
  bool allowed = cb_door_allowed();
  cb_log("door: openings %lu, last by %s, measured %lu ms, pulse errors %lu, now %s, pin %s",
         (unsigned long)guard.count(), last_who, (unsigned long)last_len_ms, (unsigned long)pulse_errors,
         allowed ? "ready" : "LOCKED OUT", gpio_get_level((gpio_num_t)CB_PIN_DOOR_DRV) ? "HIGH" : "LOW");
  cb_log("door: pulse %lu ms, max %lu ms, lock-out %lu ms (pulse fixed F-3, lock-out draft)",
         (unsigned long)cb::kDoorPulseMs, (unsigned long)cb::kDoorPulseMaxMs, (unsigned long)cb::kDoorLockoutMs);
}

}  // namespace

uint32_t cb_door_pulse_ms() {
  return pulse_setting;
}

bool cb_door_set_pulse_ms(int ms) {
  if (ms < (int)cb::kDoorPulseMinMs || ms > (int)cb::kDoorPulseMaxMs) return false;
  pulse_setting = (uint32_t)ms;
  Preferences p;
  if (p.begin("cb_set", false)) {
    p.putUInt("pulse", pulse_setting);
    p.end();
  }
  cb_log("door: pulse length set to %d ms", ms);
  return true;
}

void cb_door_begin() {
  {
    Preferences p;
    if (p.begin("cb_set", true)) {
      pulse_setting = cb::DoorGuard::clamp_pulse(p.getUInt("pulse", cb::kDoorPulseMs));
      p.end();
    }
  }
  // The pin is LOW since the static constructor in main.cpp. Re-configure as input+output (for read-back) -
  // level first, then direction, so it is never undefined.
  gpio_set_level((gpio_num_t)CB_PIN_DOOR_DRV, 0);
  gpio_set_direction((gpio_num_t)CB_PIN_DOOR_DRV, GPIO_MODE_INPUT_OUTPUT);
  gpio_set_level((gpio_num_t)CB_PIN_DOOR_DRV, 0);

  queue = xQueueCreate(2, sizeof(Request));
  guard_lock = xSemaphoreCreateMutex();
  pulse_done = xSemaphoreCreateBinary();
  gptimer_config_t tc = {};
  tc.clk_src = GPTIMER_CLK_SRC_DEFAULT;
  tc.direction = GPTIMER_COUNT_UP;
  tc.resolution_hz = 1000000;
  bool ok = gptimer_new_timer(&tc, &timer) == ESP_OK;
  gptimer_event_callbacks_t cbs = {};
  cbs.on_alarm = on_alarm;
  ok = ok && gptimer_register_event_callbacks(timer, &cbs, nullptr) == ESP_OK && gptimer_enable(timer) == ESP_OK;
  if (!ok) {
    // Without the timer there is no guaranteed end of the pulse: then the door is never opened.
    cb_log("door: ERROR - hardware timer not available, door opener DISABLED");
    queue = nullptr;
  } else {
    xTaskCreatePinnedToCore(door_task, "door", 4096, nullptr, 5, nullptr, 1);
  }
  cb_console_add("door-test", "door-test [ms] - dry run of the door opener (USB only)", cmd_door_test);
  cb_console_add("door-status", "openings, last pulse, lock-out", cmd_door_status);
  cb_log("door: %s, pin LOW", queue ? "ready" : "DISABLED");
}

bool cb_door_allowed() {
  xSemaphoreTake(guard_lock, portMAX_DELAY);
  bool a = guard.allowed(millis());
  xSemaphoreGive(guard_lock);
  return a;
}

bool cb_door_open(const char *who, uint32_t pulse_ms) {
  if (!queue) return false;
  xSemaphoreTake(guard_lock, portMAX_DELAY);
  bool a = guard.allowed(millis());
  if (a) guard.opened(millis());  // the lock-out starts with the decision, for all paths
  xSemaphoreGive(guard_lock);
  if (!a) return false;
  Request r{pulse_ms, {}};
  strncpy(r.who, who, sizeof(r.who) - 1);
  xQueueSend(queue, &r, 0);
  return true;
}

void cb_door_json(cb::Json &j) {
  // Deliberately without last_who: the outdoor unit must not show which flat opened (SA 6.4).
  j.key("door");
  j.begin_obj();
  j.kv("available", queue != nullptr);
  j.kv("ready", cb_door_allowed());
  j.kv("pin_high", gpio_get_level((gpio_num_t)CB_PIN_DOOR_DRV) == 1);
  j.kv("openings_since_boot", (unsigned long)guard.count());
  j.kv("last_pulse_ms", (unsigned long)last_len_ms);
  j.kv("pulse_errors", (unsigned long)pulse_errors);
  j.kv("pulse_ms", (unsigned long)pulse_setting);
  j.kv("lockout_ms", (unsigned long)cb::kDoorLockoutMs);
  j.end_obj();
}

#else  // indoor units have no door opener

void cb_door_json(cb::Json &) {}
void cb_door_begin() {}
uint32_t cb_door_pulse_ms() { return 0; }
bool cb_door_set_pulse_ms(int) { return false; }
bool cb_door_allowed() { return false; }
bool cb_door_open(const char *, uint32_t) { return false; }

#endif
