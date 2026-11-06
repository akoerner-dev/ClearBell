// cb_log.h - logging to the USB console that can never block the firmware.
//
// Why this exists (finding IN-B-04_Serial_bremst_Touch): Arduino `Serial` on the native USB waits up to
// ~2 s per write when a PC is attached but no terminal reads the port. A blocking log call in the touch
// or door path would stall exactly the logic that must react in milliseconds. cb_log therefore sets a TX
// timeout of 0: if the USB buffer is full, the line is dropped and counted, the caller never waits.
//
// Every line is prefixed with the uptime in seconds.milliseconds. Format strings follow printf.
#pragma once

#include <cstdarg>
#include <cstdint>

void cb_log_begin();
void cb_log(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
uint32_t cb_log_dropped();  // lines (partially) lost because nobody was reading the USB port
