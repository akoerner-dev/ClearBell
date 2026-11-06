// cb_log.cpp - non-blocking console log, see cb_log.h.
//
// Thread safety: one mutex around "format + hand to USB driver". Holding it is always short, because the
// write itself never waits (TX timeout 0). Not callable from interrupt handlers - nothing in ClearBell
// logs from an ISR.
#include "cb_log.h"

#include <Arduino.h>
#include <cstdio>

#include "cb_config.h"

namespace {
uint32_t dropped = 0;
SemaphoreHandle_t mutex = nullptr;
char line[256];  // protected by `mutex`
}  // namespace

void cb_log_begin() {
  mutex = xSemaphoreCreateMutex();
  Serial.setTxTimeoutMs(0);  // never wait for the PC (see header)
  Serial.begin(CB_CONSOLE_BAUD);
}

void cb_log(const char *fmt, ...) {
  if (!mutex) return;  // before cb_log_begin(): nothing can be printed yet anyway
  uint32_t ms = millis();
  xSemaphoreTake(mutex, portMAX_DELAY);
  int n = snprintf(line, sizeof(line), "[%5lu.%03lu] ", (unsigned long)(ms / 1000), (unsigned long)(ms % 1000));
  va_list ap;
  va_start(ap, fmt);
  // Leave room for "\r\n"; vsnprintf truncates long messages instead of overflowing.
  int m = vsnprintf(line + n, sizeof(line) - n - 2, fmt, ap);
  va_end(ap);
  size_t text = m < 0 ? 0 : (size_t)m;
  size_t room = sizeof(line) - n - 3;
  size_t len = (size_t)n + (text < room ? text : room);
  line[len++] = '\r';
  line[len++] = '\n';
  if (Serial.write((const uint8_t *)line, len) != len) dropped++;
  xSemaphoreGive(mutex);
}

uint32_t cb_log_dropped() {
  return dropped;
}
