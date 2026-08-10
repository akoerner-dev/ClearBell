/**
 * ClearBell – gemeinsame Konfiguration (Produktivfirmware) – VORLAGE
 * Transport: WLAN + UDP.
 *
 * ► Diese Datei nach  config.h  kopieren und die Platzhalter ausfüllen.
 *   config.h ist per .gitignore vom Repository ausgeschlossen und darf
 *   NIEMALS committet werden (enthält WLAN-Passwort und HMAC-Schlüssel).
 *
 * Die .ino MUSS vor  #include "config.h"  genau EINE Einheit definieren:
 *   #define CB_UNIT_AUSSEN
 *   #define CB_UNIT_INNEN_EG
 *   #define CB_UNIT_INNEN_OG
 */
#pragma once
#include <stdint.h>

// ── Firmware ─────────────────────────────────────────────────────────────────
#define CB_FW_VERSION "0.1.0-dev"

// ── Einheit prüfen + Rolle ableiten ─────────────────────────────────────────
#if (defined(CB_UNIT_AUSSEN) + defined(CB_UNIT_INNEN_EG) + defined(CB_UNIT_INNEN_OG)) != 1
  #error "Genau EINE Einheit in der .ino definieren (AUSSEN / INNEN_EG / INNEN_OG)."
#endif

// Geräte-IDs (gehen als senderId ins Paket + in den HMAC ein)
#define CB_ID_AUSSEN  1
#define CB_ID_EG      2
#define CB_ID_OG      3

// ── WLAN ─────────────────────────────────────────────────────────────────────
#define CB_WIFI_SSID       "DEIN_WLAN_SSID"
#define CB_WIFI_PASS       "DEIN_WLAN_PASSWORT"
#define CB_WIFI_TIMEOUT_MS 15000

// ── Netz / feste IPs (DHCP-Reservierung im Router) ──────────────────────────
#define CB_UDP_PORT      4210
#define CB_IP_AUSSEN     "192.168.1.50"
#define CB_IP_INNEN_EG   "192.168.1.51"
#define CB_IP_INNEN_OG   "192.168.1.52"

// ── Protokoll (validiert aus UDP_Test2) ─────────────────────────────────────
#define CB_QUITT_TIMEOUT_MS   300
#define CB_MAX_WIEDERHOLUNGEN 3
#define CB_HMAC_TAG_LEN       8     // HMAC-SHA256 auf 8 Byte gekürzt

// ── Türöffner (nur Außeneinheit ansteuernd) ─────────────────────────────────
#define CB_TUER_GPIO     21        // IRLML6344, Low-Side
#define CB_TUER_PULS_MS  2000      // Startwert, am realen Türöffner justieren

// ── I2S Audio (MAX98357A) – verifizierte Belegung, beide Einheitstypen ──────
#define CB_I2S_BCLK   26
#define CB_I2S_LRCLK  25
#define CB_I2S_DIN    22

// ── SPI SD-Karte – unterscheidet sich Außen/Innen ───────────────────────────
#if defined(CB_UNIT_AUSSEN)
  #define CB_SD_CS    5
  #define CB_SD_SCK   18
  #define CB_SD_MISO  19
  #define CB_SD_MOSI  23
#else  // beide Inneneinheiten
  #define CB_SD_CS    23
  #define CB_SD_MOSI  21
  #define CB_SD_SCK   19
  #define CB_SD_MISO  18
#endif

// ── Audio-Dateien auf SD (FAT32, WAV) ───────────────────────────────────────
#define CB_SND_KLINGEL_EG  "/klingel_eg.wav"
#define CB_SND_KLINGEL_OG  "/klingel_og.wav"
#define CB_SND_OK          "/ok.wav"       // Bestätigungston (Erfolg)
#define CB_SND_FEHLER      "/fehler.wav"   // Fehlerton (keine Quittung)

// Lautstärke 0..100 % – digitale Absenkung vor I2S (gegen Übersteuern).
// Nur der STARTWERT bei leerem NVS; im Betrieb per BOOT-Taster verstellbar.
#define CB_AUDIO_VOLUME    10

// BOOT-Taster (GPIO0, im Betrieb frei) schaltet die Lautstärke durch.
#define CB_VOL_TASTER_GPIO 0

// ── Touch ────────────────────────────────────────────────────────────────────
// Core 3.x: touchRead() ~1000er-Bereich. Schwellen nach Endmontage neu bestimmen.
#define CB_TOUCH_EIN     600
#define CB_TOUCH_AUS     800
#define CB_TOUCH_READS   2
#define CB_TOUCH_INTERVALL_MS 50
#if defined(CB_UNIT_AUSSEN)
  #define CB_TOUCH_EG_GPIO 32   // T9, Klingeltaster EG
  #define CB_TOUCH_OG_GPIO 33   // T8, Klingeltaster OG
#else
  #define CB_TOUCH_GPIO    32   // Türöffner-Taster in der Wohnung
#endif

// ── Smartphone-Push (ntfy) ──────────────────────────────────────────────────
// Topics wie Passwörter behandeln (wer das Topic kennt, kann mithören/senden).
// Eigene, schwer erratbare Topic-Namen wählen und NUR in config.h eintragen.
#define CB_NTFY_SERVER    "https://ntfy.sh"
#define CB_NTFY_TOPIC_EG  "DEIN_NTFY_MELDE_TOPIC"
#define CB_NTFY_TITLE     "ClearBell"
#define CB_NTFY_PRIORITY  "high"
#define CB_NTFY_TEXT      "Es klingelt an der Haustuer (EG)"

// Fern-Türöffnen: separates geheimes Kommando-Topic.
#define CB_NTFY_CMD_TOPIC     "DEIN_NTFY_KOMMANDO_TOPIC"
#define CB_TUER_TOKEN_TTL_MS  300000UL   // Token-Gültigkeit ~5 Min
#define CB_TUER_BTN_LABEL     "Tuer oeffnen"

// ── Geheime HMAC-Schlüssel (je Einheit eigener 32-Byte-Schlüssel) ───────────
// Eigene Zufallsschlüssel erzeugen, z. B.:
//   python -c "import os; print(', '.join('0x%02X'%b for b in os.urandom(32)))"
// Beide Einheiten eines Paares (Außen + jeweilige Innen) müssen denselben
// Schlüssel tragen. Diese Werte NUR in config.h, niemals ins Repo.
#if defined(CB_UNIT_AUSSEN) || defined(CB_UNIT_INNEN_EG)
static const uint8_t CB_KEY_EG[32] = {
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};
#endif
#if defined(CB_UNIT_AUSSEN) || defined(CB_UNIT_INNEN_OG)
static const uint8_t CB_KEY_OG[32] = {
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};
#endif
