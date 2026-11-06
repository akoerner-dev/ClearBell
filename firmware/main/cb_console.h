// cb_console.h - line-based command console on the USB serial port.
//
// Type a command and Enter in any terminal (115200 baud is ignored by native USB), or from the PC:
//   python tools/cb.py konsole COM4 "info"
// "help" lists every command. The console is for diagnosis and bring-up only: it needs physical access
// (USB cable at the unit), and it is the ONLY path for the door dry-run (Softwarearchitektur 5.4).
//
// Modules register their commands at start-up with cb_console_add(). cb_console_poll() is called from
// loop(); it never blocks - it only consumes characters that already arrived.
#pragma once

typedef void (*cb_console_fn)(const char *args);  // args: rest of the line after the command, trimmed

void cb_console_add(const char *name, const char *help, cb_console_fn fn);
void cb_console_poll();
