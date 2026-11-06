// cb_system.h - system facts and health: version, reset reason, boot counter, core dump, self-test,
// and the decision whether a freshly updated image is kept (rollback, Softwarearchitektur 8.1).
//
// Console commands: info, selftest, reboot, wdt-test, crash-test, coredump-clear (see cb_system.cpp).
#pragma once

#include <cstdint>

#include "cb_json.h"

void cb_system_begin();  // boot step 5: read reset reason, count the boot, log the identity
void cb_system_poll();   // from loop(): refreshes the "update on probation" flag (decision: cb_update)
void cb_system_selftest(bool *run, bool *ok);  // crypto self-test state, for the update probation

uint32_t cb_system_boot_count();
const char *cb_system_reset_reason();  // human-readable reason of the LAST reset
void cb_system_json(cb::Json &j);       // status page: "system": {...} (call from loop)
