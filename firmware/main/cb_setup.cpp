// cb_setup.cpp - see cb_setup.h.
#include "cb_setup.h"

#include <Arduino.h>
#include <DNSServer.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <lwip/sockets.h>

#include <atomic>
#include <cstring>

#include "cb_audio.h"
#include "cb_config.h"
#include "cb_console.h"
#include "cb_http_util.h"
#include "cb_json.h"
#include "cb_log.h"
#include "cb_modes.h"
#include "cb_net.h"
#include "cb_provisioning.h"
#include "cb_remote.h"
#include "cb_setup_logic.h"
#include "cb_setup_page.h"
#include "cb_status.h"

namespace {

const IPAddress kApIp(192, 168, 4, 1);
const IPAddress kApMask(255, 255, 255, 0);

DNSServer dns;
bool ap_on = false;
std::atomic<bool> ap_auto{false};  // open only because the WiFi is lost (R30) - read by the web task
bool ap_forced = false;          // console setup-ap on
uint32_t wifi_lost_since = 0;    // 0 = WiFi up
std::atomic<uint32_t> reboot_at{0};
char ap_ssid[24];
// Factory reset: farewell pushes first (FW-B-34, 4), the erase follows in the loop once they are out (max. 10 s)
std::atomic<bool> factory_pending{false};
uint32_t factory_since = 0;
constexpr uint32_t kFarewellWaitMs = 10000;
constexpr uint32_t kRebootPushWaitMs = 8000;  // a planned restart waits this long at most for pushes still going out

// ---- WiFi trial (SW-Entscheid_Adresse_in_Erfolgsmeldung) -------------------------------------------------------------
// The web task fills the request, the loop runs the trial; the WiFi event task only notes what happened.
portMUX_TYPE trial_lock = portMUX_INITIALIZER_UNLOCKED;
std::atomic<bool> trial_request{false};
std::atomic<int> trial_state{-1};  // -1 = none yet, else (int)cb::WifiTrial
char trial_ssid[40], trial_pass[72], trial_ip[16];  // same sizes as the form fields (checked: <= 32 / 63)
uint32_t trial_started = 0;
std::atomic<bool> trial_got_ip{false};
std::atomic<int> trial_reason{0};  // last disconnect reason, 0 = none

void wipe(char *b, size_t n) {
  volatile char *v = b;
  while (n--) *v++ = 0;
}

void on_trial_event(arduino_event_id_t e, arduino_event_info_t info) {
  if (trial_state.load() != (int)cb::WifiTrial::Running || trial_request.load()) return;
  if (e == ARDUINO_EVENT_WIFI_STA_GOT_IP) trial_got_ip = true;
  else if (e == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) {
    // 8 (ASSOC_LEAVE) and 36 (STA_LEAVING) are our own leave: aborting the attempt on the stored WiFi delivers 36
    // right after WiFi.begin() - that is not the verdict on the new WiFi (Sammeltermin Teil 3, 24.11.)
    uint8_t r = info.wifi_sta_disconnected.reason;
    if (r != 8 && r != 36) trial_reason = r ? r : 255;
  }
}

void trial_start() {
  char ssid[40], pass[72];
  taskENTER_CRITICAL(&trial_lock);
  memcpy(ssid, trial_ssid, sizeof ssid);
  memcpy(pass, trial_pass, sizeof pass);
  taskEXIT_CRITICAL(&trial_lock);
  cb_net_pause_wifi(true);
  WiFi.setAutoReconnect(false);
  WiFi.mode(WIFI_AP_STA);
  esp_wifi_set_country_code("DE", false);
  WiFi.setScanMethod(WIFI_ALL_CHANNEL_SCAN);
  WiFi.disconnect(false, false);
  trial_got_ip = false;
  trial_reason = 0;
  trial_ip[0] = 0;
  trial_started = millis();
  // The access point moves to the router's channel when the station connects - the phone reconnects to it by
  // itself, the page keeps asking for the result meanwhile.
  WiFi.begin(ssid, pass[0] ? pass : nullptr);
  wipe(pass, sizeof pass);
  cb_log("setup: trying WiFi '%s' (access point stays on)", ssid);
}

void trial_end(cb::WifiTrial v, uint8_t reason) {
  uint32_t now = millis();
  if (v == cb::WifiTrial::Connected) {
    snprintf(trial_ip, sizeof trial_ip, "%s", WiFi.localIP().toString().c_str());
    char ssid[40], pass[72];
    taskENTER_CRITICAL(&trial_lock);
    memcpy(ssid, trial_ssid, sizeof ssid);
    memcpy(pass, trial_pass, sizeof pass);
    taskEXIT_CRITICAL(&trial_lock);
    bool saved = cb_prov_set_wifi(ssid, pass);
    wipe(pass, sizeof pass);
    if (!saved) {
      cb_log("setup: WiFi '%s' works, but saving FAILED", ssid);
      v = cb::WifiTrial::Failed;
    } else {
      cb_log("setup: WiFi '%s' works (IP %s, %lu ms) - saved, restart in %lu s", ssid, trial_ip,
             (unsigned long)(now - trial_started), (unsigned long)(cb::kWifiTrialRebootMs / 1000));
      cb_setup_reboot_in(cb::kWifiTrialRebootMs);
    }
  }
  if (v != cb::WifiTrial::Connected) {
    WiFi.disconnect(false, false);
    cb_log("setup: WiFi trial failed - %s (reason %u)",
           v == cb::WifiTrial::WrongPassword ? "wrong password"
           : v == cb::WifiTrial::NotFound    ? "network not found"
                                             : "no connection",
           (unsigned)reason);
    cb_net_pause_wifi(false);
  }
  taskENTER_CRITICAL(&trial_lock);
  wipe(trial_pass, sizeof trial_pass);
  taskEXIT_CRITICAL(&trial_lock);
  trial_state = (int)v;
}

void trial_poll(uint32_t now) {
  // `now` was taken before trial_start() set trial_started: comparing in this round would wrap around and end the
  // trial at once as "timed out" (Sammeltermin Teil 3, 24.11.) - so the first check waits for the next round
  if (trial_request.exchange(false)) return trial_start();
  if (trial_state.load() != (int)cb::WifiTrial::Running) return;
  if (trial_got_ip.load()) return trial_end(cb::WifiTrial::Connected, 0);
  int r = trial_reason.exchange(0);
  if (r) return trial_end(cb::wifi_trial_verdict((uint8_t)r), (uint8_t)r);
  if (now - trial_started > cb::kWifiTrialMs) trial_end(cb::WifiTrial::Failed, 0);
}

void start_ap() {
  const CbProvisioning &p = cb_prov();
  if (!p.factory_ok) {
    static bool told = false;
    if (!told) cb_log("setup: NO device code (factory step missing) - access point not started");
    told = true;
    return;
  }
  WiFi.mode(p.valid ? WIFI_AP_STA : WIFI_AP);
  WiFi.softAPConfig(kApIp, kApIp, kApMask);
  // WPA2 with the device code: only who holds the label can join (proof of possession). Max. 2 clients.
  if (!WiFi.softAP(ap_ssid, p.dev_code, 1, 0, 2)) {
    cb_log("setup: access point FAILED to start");
    return;
  }
  dns.start(53, "*", kApIp);  // captive portal: every name -> the setup page
  ap_on = true;
  cb_log("setup: access point '%s' on (password = device code from the label), page http://192.168.4.1", ap_ssid);
}

void stop_ap() {
  if (trial_state.load() == (int)cb::WifiTrial::Running) return;  // the trial needs the access point
  dns.stop();
  WiFi.softAPdisconnect(true);
  if (cb_prov().valid) WiFi.mode(WIFI_STA);
  ap_on = false;
  cb_log("setup: access point off");
}

uint32_t ipv4_of(const struct sockaddr_storage &a) {
  if (a.ss_family == AF_INET) return ((const struct sockaddr_in *)&a)->sin_addr.s_addr;
  if (a.ss_family == AF_INET6) return ((const struct sockaddr_in6 *)&a)->sin6_addr.un.u32_addr[3];  // v4-mapped
  return 0;
}

// RV-16: local address = access point AND peer in the access point's net (not only the local address - lwIP accepts
// 192.168.4.1 on the STA interface too, weak host model).
bool request_is_ap(int sock) {
  struct sockaddr_storage local = {}, peer = {};
  socklen_t ll = sizeof local, pl = sizeof peer;
  if (getsockname(sock, (struct sockaddr *)&local, &ll) != 0 || getpeername(sock, (struct sockaddr *)&peer, &pl) != 0)
    return false;
  bool sta = cb_net_wifi_up();
  bool ok = cb::ap_request(ipv4_of(local), ipv4_of(peer), (uint32_t)kApIp, (uint32_t)kApMask,
                           sta ? (uint32_t)WiFi.localIP() : 0, sta ? (uint32_t)WiFi.subnetMask() : 0);
  if (!ok && ipv4_of(local) == (uint32_t)kApIp) {
    static uint32_t told = 0;
    if (!told || millis() - told > 60000) {
      told = millis() ? millis() : 1;
      cb_log("setup: request to 192.168.4.1 NOT from the access point net (routed in, or home net overlaps) - "
             "treated as home network");
    }
  }
  return ok;
}

// ---- HTTP handlers --------------------------------------------------------------------------------------------

esp_err_t send_json(httpd_req_t *req, const char *status, cb::Json &j) {
  httpd_resp_set_status(req, status);
  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");
  return httpd_resp_send(req, j.c_str(), j.len());
}

esp_err_t reply(httpd_req_t *req, const char *status, bool ok, const char *msg) {
  char buf[256];
  cb::Json j(buf, sizeof buf);
  j.begin_obj();
  j.kv("ok", ok);
  j.kv("message", msg);
  j.end_obj();
  return send_json(req, status, j);
}

// Who may change the setup: a client of the setup access point (knows the device code), or - from the home
// network - only in setup mode (boot button = physical presence) and after the status-page login (cb_status).
bool may_change(httpd_req_t *req) {  // for the page only ("may_change" in /setup/info); the rule: cb_access
  return cb::mode_access(cb::Endpoint::SetupSave, cb_access_input(req)).ok;
}

esp_err_t h_info(httpd_req_t *req) {
  if (!cb_status_authorize(req)) return ESP_OK;
  const CbProvisioning &p = cb_prov();
  char buf[512];
  cb::Json j(buf, sizeof buf);
  j.begin_obj();
  j.kv("serial", p.serial);
  j.kv("name", p.name);
  j.kv("image", CB_UNIT_TYPE_NAME);
  j.kv("configured", p.valid);
  j.kv("problem", p.problem ? p.problem : "");
  j.kv("ssid", p.wifi_ssid);
  j.kv("role", p.valid ? cb::unit_name(p.role) : "");
  // FW-B-21: the key this flat belongs to and its name (indoor); "" for the outdoor unit
  bool flat = p.role == cb::UnitId::EG || p.role == cb::UnitId::OG;
  j.kv("role_key", flat ? (p.role == cb::UnitId::EG ? "eg" : "og") : "");
  j.kv("key_label", flat ? cb::flat_key_label((uint8_t)p.role) : "");
  j.kv("flat_name", flat ? p.flat_name[(int)p.role] : "");
  j.kv("wifi_up", cb_net_wifi_up());
  j.kv("ip", cb_net_wifi_up() ? WiFi.localIP().toString().c_str() : "");
  char host[32];
  snprintf(host, sizeof host, "clearbell-%s.local", p.serial);
  j.kv("hostname", host);
  j.kv("mode", cb::mode_name(cb_mode()));
  cb::AccessInput ai = cb_access_input(req);
  j.kv("may_change", may_change(req));
  j.kv("may_role", cb::mode_access(cb::Endpoint::SetupRole, ai).ok);    // R30: not on the automatic access point
  j.kv("may_reset", cb::mode_access(cb::Endpoint::SetupReset, ai).ok);
  j.kv("ap_auto", ai.from_ap && ai.ap_auto);
  j.end_obj();
  return send_json(req, "200 OK", j);
}

esp_err_t h_scan(httpd_req_t *req) {
  if (!cb_status_authorize(req)) return ESP_OK;
  int n = WiFi.scanNetworks(false, false);  // blocking 2-4 s, in the web task
  static char buf[2048];
  cb::Json j(buf, sizeof buf);
  j.begin_obj();
  j.key("networks");
  j.begin_arr();
  // strongest first, each name once, hidden networks skipped
  bool used[40] = {};
  for (int k = 0; k < 20; k++) {
    int best = -1;
    for (int i = 0; i < n && i < 40; i++)
      if (!used[i] && WiFi.SSID(i).length() && (best < 0 || WiFi.RSSI(i) > WiFi.RSSI(best))) best = i;
    if (best < 0) break;
    for (int i = 0; i < n && i < 40; i++)
      if (WiFi.SSID(i) == WiFi.SSID(best)) used[i] = true;
    j.begin_obj();
    j.kv("ssid", WiFi.SSID(best).c_str());
    j.kv("rssi", (int)WiFi.RSSI(best));
    j.kv("open", WiFi.encryptionType(best) == WIFI_AUTH_OPEN);
    j.end_obj();
  }
  j.end_arr();
  j.end_obj();
  WiFi.scanDelete();
  return send_json(req, "200 OK", j);
}

// RV-17: bounded - a client that announces a body and sends nothing no longer holds the web server task.
bool read_body(httpd_req_t *req, char *buf, size_t cap) { return cb_http_read_body(req, buf, cap); }

esp_err_t h_save(httpd_req_t *req) {
  if (!cb_status_authorize(req)) return ESP_OK;
  cb::Access acc = cb::mode_access(cb::Endpoint::SetupSave, cb_access_input(req));  // RV-45
  if (!acc.ok) return reply(req, acc.status, false, acc.message);
  char body[640], ssid[40], pass[72], role[8], raw[120], name[cb::kFlatNameMaxBytes + 1];
  if (!read_body(req, body, sizeof body) || !cb::form_get(body, "ssid", ssid, sizeof ssid) ||
      !cb::form_get(body, "pass", pass, sizeof pass))
    return reply(req, "400 Bad Request", false, "Formular unvollständig");
  if (!ssid[0] || strlen(ssid) > 32) return reply(req, "400 Bad Request", false, "WLAN-Name fehlt oder zu lang");
  if (pass[0] && (strlen(pass) < 8 || strlen(pass) > 63))
    return reply(req, "400 Bad Request", false, "WLAN-Passwort: 8 bis 63 Zeichen (oder leer bei offenem WLAN)");
#if !CB_IS_OUTDOOR
  // R30: over the automatic access point only the WiFi is taken; role and flat name stay as they are
  bool may_role = cb::mode_access(cb::Endpoint::SetupRole, cb_access_input(req)).ok;
  if (may_role) {
    if (!cb::form_get(body, "role", role, sizeof role) || (strcmp(role, "eg") && strcmp(role, "og")))
      return reply(req, "400 Bad Request", false, "Bitte die Klingeltaste wählen (oben oder unten)");
    // FW-B-21: free name of the flat, optional (empty = "Wohnung oben" / "Wohnung unten")
    if (!cb::form_get(body, "name", raw, sizeof raw)) raw[0] = 0;
    if (!cb::flat_name_normalize(raw, name))
      return reply(req, "400 Bad Request", false, "Name der Wohnung: höchstens 24 Zeichen, keine Steuerzeichen");
    cb::UnitId r = strcmp(role, "eg") == 0 ? cb::UnitId::EG : cb::UnitId::OG;
    if (!cb_prov_set_role(r) || !cb_prov_set_flat_name(r, name))
      return reply(req, "500 Internal Server Error", false, "Speichern fehlgeschlagen");
  } else {
    cb_log("setup: automatic access point - only the WiFi is changed (role/name need setup mode, R30)");
  }
#else
  (void)role, (void)raw, (void)name;
#endif
  // SW-Entscheid_Adresse_in_Erfolgsmeldung: over the access point the WiFi is tried first; the page asks
  // /setup/result and shows wrong password / not found / the real address. From the home network (setup mode) the
  // trial would cut the page off its own connection - there it is stored and the unit restarts as before.
  if (cb_setup_from_ap(req)) {
    if (trial_state.load() == (int)cb::WifiTrial::Running || trial_request.load())
      return reply(req, "409 Conflict", false, "Es läuft schon ein Verbindungsversuch – bitte kurz warten.");
    taskENTER_CRITICAL(&trial_lock);
    snprintf(trial_ssid, sizeof trial_ssid, "%s", ssid);
    snprintf(trial_pass, sizeof trial_pass, "%s", pass);
    taskEXIT_CRITICAL(&trial_lock);
    wipe(pass, sizeof pass);
    trial_request = true;  // the loop starts the attempt; the page polls /setup/result at once
    trial_state = (int)cb::WifiTrial::Running;
    char m[200];
    snprintf(m, sizeof m, "Verbinde mit „%s“ … (bis zu 30 s; die Verbindung zum Handy kann kurz abreißen)", ssid);
    char buf[300];
    cb::Json j(buf, sizeof buf);
    j.begin_obj();
    j.kv("ok", true);
    j.kv("trial", true);
    j.kv("message", m);
    j.end_obj();
    return send_json(req, "200 OK", j);
  }
  if (!cb_prov_set_wifi(ssid, pass)) return reply(req, "500 Internal Server Error", false, "Speichern fehlgeschlagen");
  cb_log("setup: WiFi '%s' saved over the setup page - rebooting", ssid);
  char msg[160];
  snprintf(msg, sizeof msg, "Gespeichert. Die Einheit startet neu und verbindet sich mit „%s“. Danach: "
           "http://clearbell-%s.local", ssid, cb_prov().serial);
  cb_setup_reboot_in(3000);
  return reply(req, "200 OK", true, msg);
}

// SW-Entscheid_Adresse_in_Erfolgsmeldung: how the WiFi trial went. German messages, the wording the buyer asked for.
esp_err_t h_result(httpd_req_t *req) {
  if (!cb_status_authorize(req)) return ESP_OK;
  int st = trial_state.load();
  char ssid[40], m[300], url[40] = "", url2[48] = "";
  taskENTER_CRITICAL(&trial_lock);
  memcpy(ssid, trial_ssid, sizeof ssid);
  taskEXIT_CRITICAL(&trial_lock);
  const char *state = "none";
  m[0] = 0;
  switch (st) {
    case (int)cb::WifiTrial::Running:
      state = "running";
      snprintf(m, sizeof m, "Verbinde mit „%s“ …", ssid);
      break;
    case (int)cb::WifiTrial::Connected:
      state = "connected";
      // The address is the one the router gave during the trial; after the restart most routers give the same one
      // again, but not all - so the name (mDNS, independent of the address) comes as a second link
      snprintf(url, sizeof url, "http://%s", trial_ip);
      snprintf(url2, sizeof url2, "http://clearbell-%s.local", cb_prov().serial);
      snprintf(m, sizeof m, "Verbunden mit „%s“. Die Einheit startet in %lu s neu. Danach das Handy wieder ins "
               "Heim-WLAN wechseln und die Adresse öffnen; falls sie nicht geht, den Namen darunter.", ssid,
               (unsigned long)(cb::kWifiTrialRebootMs / 1000));
      break;
    case (int)cb::WifiTrial::WrongPassword:
      state = "wrong_password";
      snprintf(m, sizeof m, "Falsche Anmeldedaten: „%s“ hat das Passwort nicht angenommen. Bitte prüfen und neu "
               "versuchen.", ssid);
      break;
    case (int)cb::WifiTrial::NotFound:
      state = "not_found";
      snprintf(m, sizeof m, "WLAN „%s“ nicht gefunden: Namen prüfen (Groß-/Kleinschreibung), und ob der Router in "
               "Reichweite ist und 2,4 GHz anbietet.", ssid);
      break;
    case (int)cb::WifiTrial::Failed:
      state = "failed";
      snprintf(m, sizeof m, "Keine Verbindung mit „%s“ zustande gekommen. Bitte neu versuchen.", ssid);
      break;
  }
  char buf[480];
  cb::Json j(buf, sizeof buf);
  j.begin_obj();
  j.kv("state", state);
  j.kv("ok", st == (int)cb::WifiTrial::Connected);
  j.kv("message", m);
  j.kv("url", url);
  j.kv("url2", url2);
  j.end_obj();
  return send_json(req, "200 OK", j);
}

esp_err_t h_reset(httpd_req_t *req) {
  if (!cb_status_authorize(req)) return ESP_OK;
  cb::Access acc = cb::mode_access(cb::Endpoint::SetupReset, cb_access_input(req));  // RV-45
  if (!acc.ok) return reply(req, acc.status, false, acc.message);
  char body[64], confirm[8];
  if (!read_body(req, body, sizeof body) || !cb::form_get(body, "confirm", confirm, sizeof confirm) ||
      strcmp(confirm, "JA") != 0)
    return reply(req, "400 Bad Request", false, "Bestätigung fehlt");
  reply(req, "200 OK", true, "Werkszustand wird hergestellt, die Einheit startet neu.");
  cb_setup_factory_reset();
  return ESP_OK;
}

// Captive portal: phones probe a known URL; any answer other than the expected one opens the setup page.
esp_err_t h_catchall(httpd_req_t *req) {
  if (!cb_setup_from_ap(req)) {
    httpd_resp_set_status(req, "404 Not Found");
    return httpd_resp_sendstr(req, "not found\n");
  }
  httpd_resp_set_status(req, "302 Found");
  httpd_resp_set_hdr(req, "Location", "http://192.168.4.1/");
  return httpd_resp_send(req, nullptr, 0);
}

// ---- Console (development / factory) ------------------------------------------------------------------------------

void cmd_setup_wifi(const char *args) {
  const char *sp = strrchr(args, ' ');
  if (!sp) {
    cb_log("usage: setup-wifi <ssid> <password>   (development/factory - buyers use the setup page)");
    return;
  }
  char ssid[40];
  snprintf(ssid, sizeof ssid, "%.*s", (int)(sp - args), args);
  bool ok = cb_prov_set_wifi(ssid, sp + 1);
  cb_log("setup-wifi: %s - reboot to apply", ok ? "saved" : "FAILED");
}

void cmd_setup_role(const char *args) {
  cb::UnitId r = !strcmp(args, "eg") ? cb::UnitId::EG : !strcmp(args, "og") ? cb::UnitId::OG : cb::UnitId::None;
  cb_log("setup-role: %s", r != cb::UnitId::None && cb_prov_set_role(r) ? "saved - reboot to apply"
                                                                        : "usage: setup-role eg|og (indoor only)");
}

void cmd_setup_name(const char *args) {
  char name[cb::kFlatNameMaxBytes + 1];
  const CbProvisioning &p = cb_prov();
  if (CB_IS_OUTDOOR || (p.role != cb::UnitId::EG && p.role != cb::UnitId::OG)) {
    cb_log("setup-name: indoor units with a chosen key only (setup-role first)");
    return;
  }
  if (!cb::flat_name_normalize(args, name)) {
    cb_log("setup-name: not a valid name (max. 24 characters / 40 bytes UTF-8, no control characters)");
    return;
  }
  bool ok = cb_prov_set_flat_name(p.role, name);
  cb_log("setup-name: %s '%s' - the outdoor unit gets it after the next start", ok ? "saved" : "FAILED",
         cb_prov_flat_name(p.role));
}

void cmd_wifi_scan(const char *) {
  int n = WiFi.scanNetworks(false, true);
  cb_log("wifi-scan: %d network(s)", n);
  for (int i = 0; i < n; i++)
    cb_log("  %-32s %4d dBm ch %2d %s", WiFi.SSID(i).length() ? WiFi.SSID(i).c_str() : "(hidden)", (int)WiFi.RSSI(i),
           (int)WiFi.channel(i), WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "OPEN" : "secured");
  WiFi.scanDelete();
}

void cmd_setup_ap(const char *args) {
  ap_forced = !strcmp(args, "on");
  cb_log("setup-ap: %s", ap_forced ? "forced on" : "automatic");
}

}  // namespace

void cb_setup_begin() {
  snprintf(ap_ssid, sizeof ap_ssid, "ClearBell-%s", cb_prov().serial);
  cb_console_add("setup-wifi", "setup-wifi <ssid> <password> - DEV/factory: WiFi without the setup page",
                 cmd_setup_wifi);
  cb_console_add("setup-role", "setup-role eg|og - DEV/factory: key of an indoor unit (eg = bottom, og = top)",
                 cmd_setup_role);
  cb_console_add("setup-name", "setup-name <name> - DEV: free flat name of an indoor unit, empty = default",
                 cmd_setup_name);
  cb_console_add("wifi-scan", "list the WiFi networks in range (incl. setup access points)", cmd_wifi_scan);
  cb_console_add("setup-ap", "setup-ap on|auto - TEST: force the setup access point", cmd_setup_ap);
  WiFi.onEvent(on_trial_event);  // SW-Entscheid: the WiFi trial listens itself (cb_net may not run yet)
  if (!cb_prov().valid) start_ap();  // first setup: also brings up the network stack for the web server
  wifi_lost_since = millis();
}

void cb_setup_poll() {
  uint32_t now = millis();
  if (factory_pending.load() && (cb_remote_idle() || now - factory_since > kFarewellWaitMs)) {
    factory_pending = false;
    cb_prov_factory_reset();
    cb_audio_restore_factory_tones();
    cb_setup_reboot_in(1500);
  }
  uint32_t r = reboot_at.load();
  // a farewell or restart notice still on its way may delay the restart a few seconds (FW-B-34, 4)
  if (r && (int32_t)(now - r) >= 0 && (cb_remote_idle() || (int32_t)(now - r) >= (int32_t)kRebootPushWaitMs)) {
    cb_log("setup: rebooting");
    delay(50);
    esp_restart();
  }
  trial_poll(now);
  bool up = cb_prov().valid && cb_net_wifi_up();
  if (up) wifi_lost_since = 0;
  else if (!wifi_lost_since) wifi_lost_since = now ? now : 1;
  bool want_setup = !cb_prov().valid || cb_mode() == cb::Mode::Setup || ap_forced;
  bool want_auto = wifi_lost_since && cb::auto_ap_due(now - wifi_lost_since);  // R30: 5 min lost, then max. 30 min
  bool want = want_setup || want_auto;
  bool was_auto = ap_auto.load();
  ap_auto = want && !want_setup;
  if (want && !ap_on) start_ap();
  else if (!want && ap_on) {
    if (was_auto && wifi_lost_since) cb_log("setup: automatic access point closed after 30 min (R30) - button for more");
    stop_ap();
  }
}

void cb_setup_register(httpd_handle_t s) {
  httpd_uri_t info = {"/setup/info", HTTP_GET, h_info, nullptr};
  httpd_uri_t scan = {"/setup/scan", HTTP_GET, h_scan, nullptr};
  httpd_uri_t save = {"/setup/save", HTTP_POST, h_save, nullptr};
  httpd_uri_t reset = {"/setup/reset", HTTP_POST, h_reset, nullptr};
  httpd_uri_t result = {"/setup/result", HTTP_GET, h_result, nullptr};
  httpd_register_uri_handler(s, &result);
  httpd_register_uri_handler(s, &info);
  httpd_register_uri_handler(s, &scan);
  httpd_register_uri_handler(s, &save);
  httpd_register_uri_handler(s, &reset);
}

void cb_setup_register_catchall(httpd_handle_t s) {
  httpd_uri_t any = {"/*", HTTP_GET, h_catchall, nullptr};
  httpd_register_uri_handler(s, &any);
}

bool cb_setup_from_ap(httpd_req_t *req) {
  return ap_on && request_is_ap(httpd_req_to_sockfd(req));
}

bool cb_setup_ap_on() {
  return ap_on;
}

bool cb_setup_ap_auto() {
  return ap_on && ap_auto.load();
}

void cb_setup_factory_reset() {
  if (factory_pending.exchange(true)) return;
  factory_since = millis();
  cb_remote_farewell_all("die Einheit wurde auf Werkszustand gesetzt");  // FW-B-34 (4), before the keys are gone
  cb_log("setup: factory reset - farewell to the stored phones first (max. %lu s)",
         (unsigned long)(kFarewellWaitMs / 1000));
}

void cb_setup_reboot_in(uint32_t ms) {
  reboot_at = millis() + ms;
}
