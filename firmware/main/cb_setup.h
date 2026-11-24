// cb_setup.h - setup without PC: setup access point, captive portal, setup page (port step 10a).
//
// Buyer's view (docs/Produktreife.md 3.2):
//   1. A unit that is not set up opens the WiFi "ClearBell-<unit number>", WPA2 password = device code from the
//      label. The label's QR code joins it directly (WIFI:T:WPA;S:...;P:...;;). Phones then show the setup page by
//      themselves (captive portal: every DNS name answers 192.168.4.1), otherwise http://192.168.4.1.
//   2. Choose the home WiFi from the list, password, indoor also the flat (EG/OG) -> "Speichern". Over the access point
//      the unit first TRIES the WiFi (access point + station at once) and the page shows "Falsche Anmeldedaten", "nicht
//      gefunden" or the real address as a link (SW-Entscheid_Adresse_in_Erfolgsmeldung, /setup/result); only a working
//      WiFi is stored, the restart follows 45 s later. Reachable at http://clearbell-<unit number>.local as well.
//
// The access point is also opened
//   - in setup mode (boot button 5 s twice) - e.g. to change the WiFi; closes after 30 min or a short press,
//   - automatically when a set-up unit has had no WiFi for 5 min (router replaced, password changed): no button,
//     no opening of the outdoor housing. It closes again as soon as the home WiFi is back, at the latest after
//     30 min per WiFi loss (R30, cb_setup_logic.h). Over this automatic access point ONLY the WiFi can be changed:
//     role, flat name, factory reset and pairing need setup mode (button) - cb_access.h.
// Clients of the access point need no further login - knowing the device code (WPA2) is the proof of possession,
// like Espressif's provisioning. Exception (R30): on the automatic access point an own status-page password is asked
// for, if one is set. From the home network the setup page always asks for the status-page password. "Came over the
// access point" needs the local AP address AND a peer in the AP net (RV-16, ap_request()).
//
// Factory reset: in setup mode hold the boot button 15 s, or the button on the setup page (setup mode only).
//
// Console (USB, development/factory): setup-wifi <ssid> <password> · setup-role eg|og · wifi-scan · setup-ap on|off
#pragma once

#include <esp_http_server.h>

void cb_setup_begin();   // after cb_modes_begin, BEFORE cb_status_begin (starts the network stack if needed)
void cb_setup_poll();    // from loop(): access point on/off, scheduled reboot
void cb_setup_register(httpd_handle_t server);  // setup handlers; cb_status calls it (catch-all is registered last)
void cb_setup_register_catchall(httpd_handle_t server);
bool cb_setup_from_ap(httpd_req_t *req);         // request came in over the setup access point
bool cb_setup_ap_on();
bool cb_setup_ap_auto();  // the access point is open ONLY because the WiFi is lost (R30: WiFi change only)
void cb_setup_factory_reset();                   // erase buyer data, restore factory tones, reboot
void cb_setup_reboot_in(uint32_t ms);
