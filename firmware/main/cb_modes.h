// cb_modes.h - operating modes on the device: samples the boot button, runs cb::ModeMachine
// (components/cb_core/include/cb_modes_logic.h explains the modes), drives the LED pattern of the mode
// and registers the console commands that are only allowed in maintenance or setup.
#pragma once

#include "cb_led.h"
#include "cb_modes_logic.h"

void cb_modes_begin(bool provisioned);
void cb_modes_poll();
cb::Mode cb_mode();

// What the LED shows in normal mode (green / red / amber - decided by the application in main.cpp).
// In maintenance and setup the mode pattern wins.
void cb_modes_set_normal_led(CbLed pattern);
// Something was done in maintenance (e.g. a tone upload): restart its 10-minute window. Call from loop only.
void cb_modes_activity();
// Outdoor: a paired indoor unit asked for maintenance (port step 10b). Call from loop. false in setup mode.
bool cb_modes_request_maintenance();
