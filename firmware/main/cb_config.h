// cb_config.h - pins, timing constants and unit type. NO secrets in here (those come from provisioning).
//
// Pin names = net names of the KiCad schematics (Softwarearchitektur 2.3 and 9), prefixed with CB_PIN_.
// Both units share one pin set on purpose; where a function does not exist indoors, the pin stays
// unused instead of being re-assigned. All pin assignments from V0.1 are invalid.
//
// Never configure GPIO35/36/37 (octal PSRAM), 19/20 (USB), 39-42 (JTAG, no copper) or 43/44 (UART0
// test points, fallback flash path). Reserve pins 1, 2, 8-13 and 6/14 (guard/shield) stay untouched.
#pragma once

#include <sdkconfig.h>

// ---- Unit type: exactly one per image, checked at compile time (Softwarearchitektur 9) -------------
#if defined(CONFIG_CB_UNIT_OUTDOOR) == defined(CONFIG_CB_UNIT_INDOOR)
#error "Exactly one of CONFIG_CB_UNIT_OUTDOOR / CONFIG_CB_UNIT_INDOOR must be set (use tools/cb.py)"
#endif

#if defined(CONFIG_CB_UNIT_OUTDOOR)
#define CB_IS_OUTDOOR 1
#define CB_UNIT_TYPE_NAME "aussen"
#else
#define CB_IS_OUTDOOR 0
#define CB_UNIT_TYPE_NAME "innen"
#endif

// ---- Pins (net name in the schematic -> GPIO) -------------------------------------------------------
#if CB_IS_OUTDOOR
#define CB_PIN_DOOR_DRV 47   // door opener MOSFET gate driver. ONLY cb_door may touch it (after boot step 1).
#define CB_PIN_TOUCH_EG 4    // bell key ground-floor flat (TOUCH4)
#define CB_PIN_TOUCH_OG 5    // bell key upper-floor flat (TOUCH5)
#else
#define CB_PIN_TOUCH 4       // door-opener key of the indoor unit (TOUCH4); IO5 and IO47 are not connected
#endif
#define CB_PIN_I2S_BCLK 15   // I2S1 bit clock -> MAX98357A
#define CB_PIN_I2S_WS 16     // I2S1 word select / LRCLK -> MAX98357A
#define CB_PIN_I2S_DOUT 17   // I2S1 data -> MAX98357A DIN
#define CB_PIN_AMP_SD_MODE 7 // amplifier shutdown: LOW = off/mute (pull-down keeps it off during boot)
#define CB_PIN_MIC_CLK 38    // PDM clock -> microphone (I2S0). Not started in normal operation (5.7)
#define CB_PIN_MIC_DATA 21   // PDM data <- microphone
#define CB_PIN_LED_RED 48    // status LED red, active HIGH
#define CB_PIN_LED_GRN 18    // status LED green, active HIGH
#define CB_PIN_BOOT 0        // S_BOOT1 (IO0), pressed = LOW. Pressed at power-on = download mode!

// ---- Serial console ---------------------------------------------------------------------------------
#define CB_CONSOLE_BAUD 115200  // irrelevant for native USB, kept for the UART0 fallback path

// ---- Door ---------------------------------------------------------------------------------------------
// Pulse length, hard maximum and lock-out: cb_door_logic.h (kDoorPulseMs, kDoorPulseMaxMs, kDoorLockoutMs).

// ---- Watchdog ---------------------------------------------------------------------------------------
// Must match CONFIG_ESP_TASK_WDT_TIMEOUT_S in sdkconfig.defaults; repeated here only for the log line.
#define CB_WDT_TIMEOUT_S 5
