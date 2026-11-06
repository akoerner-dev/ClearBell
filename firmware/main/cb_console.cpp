// cb_console.cpp - see cb_console.h.
#include "cb_console.h"

#include <Arduino.h>
#include <cstring>

#include "cb_log.h"

namespace {

struct Command {
  const char *name;
  const char *help;
  cb_console_fn fn;
};

constexpr int kMaxCommands = 48;
Command commands[kMaxCommands];
int n_commands = 0;

char buf[96];
size_t buf_len = 0;

void cmd_help(const char *) {
  cb_log("commands:");
  for (int i = 0; i < n_commands; i++) cb_log("  %-12s %s", commands[i].name, commands[i].help);
}

void dispatch(char *text) {
  while (*text == ' ') text++;
  if (!*text) return;
  char *args = text;
  while (*args && *args != ' ') args++;
  if (*args) *args++ = 0;
  while (*args == ' ') args++;
  for (char *e = args + strlen(args); e > args && e[-1] == ' ';) *--e = 0;
  for (int i = 0; i < n_commands; i++) {
    if (strcmp(commands[i].name, text) == 0) {
      commands[i].fn(args);
      return;
    }
  }
  cb_log("unknown command '%s' - try 'help'", text);
}

}  // namespace

void cb_console_add(const char *name, const char *help, cb_console_fn fn) {
  if (n_commands == 0) {
    commands[n_commands++] = {"help", "list all commands", cmd_help};
  }
  if (n_commands < kMaxCommands) commands[n_commands++] = {name, help, fn};
}

void cb_console_poll() {
  // Bounded work per call: at most one buffer's worth of characters, so a flood on the USB port cannot
  // keep loop() busy.
  for (int budget = sizeof(buf); budget > 0 && Serial.available() > 0; budget--) {
    int c = Serial.read();
    if (c == '\r' || c == '\n') {
      buf[buf_len] = 0;
      buf_len = 0;
      dispatch(buf);
    } else if (c >= ' ' && buf_len < sizeof(buf) - 1) {
      buf[buf_len++] = (char)c;
    }
  }
}
