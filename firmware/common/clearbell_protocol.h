/**
 * ClearBell – Gemeinsames Kommunikationsprotokoll
 * Wird von Außen- und Inneneinheit gleich verwendet.
 *
 * Übertragungsweg: ESP-NOW (IEEE 802.11, kein Router nötig)
 */

#pragma once
#include <stdint.h>

// ── Geräte-IDs ──────────────────────────────────────────────────────────────
#define DEVICE_AUSSENEINHEIT  0x01
#define DEVICE_INNENEINHEIT_1 0x02  // z.B. EG
#define DEVICE_INNENEINHEIT_2 0x03  // z.B. OG

// ── Event-Typen ─────────────────────────────────────────────────────────────
typedef enum : uint8_t {
    EVENT_RING       = 0x01,  // Klingeltaste gedrückt
    EVENT_ACK        = 0x02,  // Empfangsbestätigung
    EVENT_DOOR_OPEN  = 0x03,  // Türöffner auslösen (zukünftig)
    EVENT_SILENT_ON  = 0x04,  // Stummschaltung ein
    EVENT_SILENT_OFF = 0x05,  // Stummschaltung aus
} ClearBellEvent;

// ── Touch-Zonen (nur Außeneinheit hat 2 Zonen) ──────────────────────────────
typedef enum : uint8_t {
    ZONE_EG = 0x01,  // Erdgeschoss-Taste
    ZONE_OG = 0x02,  // Obergeschoss-Taste
    ZONE_NA = 0x00,  // Nicht zutreffend
} TouchZone;

// ── Nachrichtenstruktur (max. 250 Byte für ESP-NOW) ─────────────────────────
typedef struct __attribute__((packed)) {
    uint8_t       senderID;    // DEVICE_*
    ClearBellEvent event;       // EVENT_*
    TouchZone     zone;         // ZONE_*
    uint32_t      timestamp_ms; // millis() des Senders
} ClearBellMessage;

// Größencheck (ESP-NOW max. 250 Byte)
static_assert(sizeof(ClearBellMessage) <= 250, "ClearBellMessage zu groß für ESP-NOW");
