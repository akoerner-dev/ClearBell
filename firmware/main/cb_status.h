// cb_status.h - status page and log of door openings. Port step 8 (Softwarearchitektur 6.3, 6.4, 8.3).
//
// Status page: http://clearbell-<unit number>.local/ (or the IP from the console "net"), user "clearbell",
// password = device code from the label (since 10a; the buyer may later set an own password - planned).
// HTTP Digest login (cb_http_auth.h): the password never crosses the WiFi in clear text.
//
//   GET  /             the page itself (German, reloads its data every 5 s)
//   GET  /status.json  everything the page shows, as JSON - also for scripts (cb.py status)
//   PUT  /ton/<name>   replace a tone (ring | ok | error | nonet | tick), ONLY in maintenance mode
//
// What the page shows (SA 8.3): radio, protocol counters, touch raw value and baseline per key, door log
// (per unit only what it may see, SA 6.4), audio, system. What it can NEVER do: open the door (R17) - there is
// no such handler, and no handler calls cb_door. Read-only in normal mode; writing (tone upload) only in
// maintenance, i.e. after someone held the boot button inside the housing for 5 s (physical presence).
//
// Threads: the HTTP server runs in its own task (IDF esp_http_server). It never touches the modules' state
// directly: for /status.json it asks loop() for a snapshot (cb_status_poll) and waits up to 1 s, so every
// value is read by the task that owns it (SA 3.2). Tone uploads go through cb_audio_install(), which swaps
// the file only while no tone plays.
#pragma once

#include "cb_door_log.h"

void cb_status_begin();  // after cb_net_begin: loads the door log, starts the web server (needs provisioning)
void cb_status_poll();   // from loop(): builds a snapshot when the web server asks for one

// Door log (SA 6.4). Call from loop() only.
void cb_status_door_event(cb::DoorEvent e);  // one entry with time (indoor: own results; outdoor: service/remote)
void cb_status_door_local();                 // outdoor: opening requested by an indoor key -> sum only

// Current time for logs: Unix seconds once NTP has set the clock (wall = true), else seconds since boot.
uint32_t cb_status_now(bool *wall);

bool cb_status_web_running();
// Login check for other modules' web handlers (cb_setup). false = the 401/429 answer has been sent.
#include <esp_http_server.h>
bool cb_status_authorize(httpd_req_t *req);  // the update path works (part of the update probation, cb_update)
