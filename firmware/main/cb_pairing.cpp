// cb_pairing.cpp - see cb_pairing.h.
#include "cb_pairing.h"

#include <Arduino.h>
#include <ESPmDNS.h>
#include <esp_http_client.h>

#include <atomic>
#include <cstring>

#include "cb_config.h"
#include "cb_crypto.h"
#include "cb_door.h"
#include "cb_log.h"
#include "cb_modes.h"
#include "cb_net.h"
#include "cb_pairing_logic.h"
#include "cb_provisioning.h"
#include "cb_remote_logic.h"
#include "cb_setup.h"
#include "cb_setup_logic.h"
#include "cb_status.h"

namespace {

constexpr int kMaxFailures = 5;
constexpr uint32_t kLockMs = 10UL * 60 * 1000;
std::atomic<int> failures{0};
std::atomic<uint32_t> locked_until{0};
std::atomic<bool> maint_request{false};  // web task -> loop (indoor: send the event)
#if !CB_IS_OUTDOOR
// Indoor: the flat name still has to reach the outdoor unit (FW-B-21) - after every start and every change.
constexpr uint32_t kNameRetryMs = 30000;
std::atomic<bool> name_pending{true};
bool name_in_flight = false;
uint32_t name_next_try = 0;
#endif

esp_err_t reply(httpd_req_t *req, const char *status, bool ok, const char *msg, const char *extra_key = nullptr,
                const char *extra_val = nullptr, const char *extra2_key = nullptr, const char *extra2_val = nullptr) {
  char buf[400];
  cb::Json j(buf, sizeof buf);
  j.begin_obj();
  j.kv("ok", ok);
  j.kv("message", msg);
  if (extra_key) j.kv(extra_key, extra_val);
  if (extra2_key) j.kv(extra2_key, extra2_val);
  j.end_obj();
  httpd_resp_set_status(req, status);
  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");
  return httpd_resp_send(req, j.c_str(), j.len());
}

bool read_body(httpd_req_t *req, char *buf, size_t cap) {
  size_t len = req->content_len;
  if (len == 0 || len >= cap) return false;
  size_t got = 0;
  while (got < len) {
    int n = httpd_req_recv(req, buf + got, len - got);
    if (n == HTTPD_SOCK_ERR_TIMEOUT) continue;
    if (n <= 0) return false;
    got += n;
  }
  buf[got] = 0;
  return true;
}

// ---- Indoor: accept a pairing offer (no login: the offer itself proves knowledge of THIS unit's code) -----------

#if !CB_IS_OUTDOOR
const char *flat_txt(cb::UnitId u) {
  return u == cb::UnitId::EG ? "eg" : u == cb::UnitId::OG ? "og" : "aussen";
}

esp_err_t h_pair_accept(httpd_req_t *req) {
  const CbProvisioning &p = cb_prov();
  uint32_t now = millis();
  uint32_t lu = locked_until.load();
  if (lu && (int32_t)(lu - now) > 0) return reply(req, "429 Too Many Requests", false, "gesperrt - später erneut");
  bool ready = !p.has_key[(int)cb::UnitId::Outdoor] || cb_mode() != cb::Mode::Normal;
  if (!ready)
    return reply(req, "409 Conflict", false,
                 "Inneneinheit ist schon gekoppelt - zum Neu-Koppeln dort Wartung öffnen (Boot-Taster 5 s)");
  if (!p.valid) return reply(req, "409 Conflict", false, "Inneneinheit ist noch nicht eingerichtet (WLAN, Wohnung)");
  char body[400], au[8], in[8], n[40], e[72], t[72];
  cb::PairOffer o;
  if (!read_body(req, body, sizeof body) || !cb::form_get(body, "au", au, sizeof au) ||
      !cb::form_get(body, "in", in, sizeof in) || !cb::form_get(body, "n", n, sizeof n) ||
      !cb::form_get(body, "e", e, sizeof e) || !cb::form_get(body, "t", t, sizeof t) ||
      !cb::from_hex(n, o.nonce, sizeof o.nonce) || !cb::from_hex(e, o.enc, 32) || !cb::from_hex(t, o.tag, 32) ||
      strlen(au) != 6)
    return reply(req, "400 Bad Request", false, "Kopplungsanfrage unvollständig");
  uint8_t key[32];
  if (strcmp(in, p.serial) != 0 || !cb::pair_open(p.dev_code, au, p.serial, o, key)) {
    if (++failures >= kMaxFailures) {
      failures = 0;
      locked_until = now + kLockMs;
      cb_log("pairing: %d wrong attempts - locked for 10 min", kMaxFailures);
    }
    cb_log("pairing: offer from outdoor unit %s REFUSED (wrong code or other unit)", au);
    return reply(req, "403 Forbidden", false, "Gerätecode passt nicht");
  }
  failures = 0;
  uint8_t c[32];
  char ch[65];
  cb::pair_confirm(key, o.nonce, (uint8_t)p.role, c);
  cb::to_hex_lower(c, 32, ch);
  if (!cb_prov_set_link(cb::UnitId::Outdoor, key, au)) return reply(req, "500 Internal Server Error", false, "Speichern fehlgeschlagen");
  memset(key, 0, sizeof key);
  cb_log("pairing: paired with outdoor unit %s as %s - restarting", au, cb::unit_name(p.role));
  cb_setup_reboot_in(2500);
  return reply(req, "200 OK", true, "gekoppelt", "flat", flat_txt(p.role), "c", ch);
}

// The flat's own name (FW-B-21), changed in maintenance or setup (button in the flat = presence).
esp_err_t h_flat_name(httpd_req_t *req) {
  if (!cb_status_authorize(req)) return ESP_OK;
  const CbProvisioning &p = cb_prov();
  if (cb_mode() == cb::Mode::Normal && !cb_setup_from_ap(req))
    return reply(req, "403 Forbidden", false, "nur in der Wartung dieser Inneneinheit (Boot-Taster 5 s)");
  if (p.role != cb::UnitId::EG && p.role != cb::UnitId::OG)
    return reply(req, "409 Conflict", false, "erst die Klingeltaste wählen (Einrichtung)");
  char body[200], raw[120], name[cb::kFlatNameMaxBytes + 1];
  if (!read_body(req, body, sizeof body) || !cb::form_get(body, "name", raw, sizeof raw))
    return reply(req, "400 Bad Request", false, "Name fehlt");
  if (!cb::flat_name_normalize(raw, name))
    return reply(req, "400 Bad Request", false, "Name: höchstens 24 Zeichen, keine Steuerzeichen");
  if (!cb_prov_set_flat_name(p.role, name)) return reply(req, "500 Internal Server Error", false, "Speichern fehlgeschlagen");
  name_pending = true;  // loop sends it to the outdoor unit
  cb_net_mdns_update_name();
  cb_log("pairing: flat name set to '%s'", cb_prov_flat_name(p.role));
  char msg[120];
  snprintf(msg, sizeof msg, "Name gespeichert: „%s“", cb_prov_flat_name(p.role));
  return reply(req, "200 OK", true, msg);
}

esp_err_t h_outdoor_maint(httpd_req_t *req) {
  if (!cb_status_authorize(req)) return ESP_OK;
  if (cb_mode() != cb::Mode::Maintenance)
    return reply(req, "403 Forbidden", false, "nur in der Wartung dieser Inneneinheit (Boot-Taster 5 s)");
  if (!cb_prov().has_key[(int)cb::UnitId::Outdoor]) return reply(req, "409 Conflict", false, "nicht gekoppelt");
  maint_request = true;  // the protocol engine belongs to loop()
  return reply(req, "200 OK", true, "Anfrage gesendet - die Außeneinheit ist gleich 10 min in der Wartung");
}
#endif

// ---- Outdoor: find indoor units, pair, unpair, pulse length ----------------------------------------------------

#if CB_IS_OUTDOOR
esp_err_t h_pair_list(httpd_req_t *req) {
  if (!cb_status_authorize(req)) return ESP_OK;
  int n = MDNS.queryService("clearbell", "udp");  // blocking ~3 s, web task
  static char buf[1500];
  cb::Json j(buf, sizeof buf);
  j.begin_obj();
  j.key("units");
  j.begin_arr();
  for (int k = 0; k < n && k < 8; k++) {
    String role = MDNS.txt(k, "role");
    if (role != "eg" && role != "og") continue;
    j.begin_obj();
    j.kv("id", MDNS.txt(k, "id").c_str());
    j.kv("flat", role.c_str());
    // FW-B-21: the indoor unit's own name and its key, so the buyer recognises the unit in the list
    String name = MDNS.txt(k, "name");
    char n[cb::kFlatNameMaxBytes + 1];
    uint8_t f = role == "eg" ? (uint8_t)cb::UnitId::EG : (uint8_t)cb::UnitId::OG;
    j.kv("name", cb::flat_name_normalize(name.c_str(), n) && n[0] ? n : cb::flat_default_name(f));
    j.kv("key", cb::flat_key_label(f));
    j.kv("ip", MDNS.address(k).toString().c_str());
    j.kv("paired", MDNS.txt(k, "paired") == "1");
    j.end_obj();
  }
  j.end_arr();
  j.end_obj();
  httpd_resp_set_type(req, "application/json");
  return httpd_resp_send(req, j.c_str(), j.len());
}

// Posts the sealed offer to the indoor unit; returns its HTTP status and fills the answer body.
int post_offer(const char *ip, const char *form, char *answer, size_t cap) {
  char url[48];
  snprintf(url, sizeof url, "http://%s/pair", ip);
  esp_http_client_config_t cfg = {};
  cfg.url = url;
  cfg.method = HTTP_METHOD_POST;
  cfg.timeout_ms = 6000;
  esp_http_client_handle_t c = esp_http_client_init(&cfg);
  if (!c) return 0;
  esp_http_client_set_header(c, "Content-Type", "application/x-www-form-urlencoded");
  int status = 0;
  if (esp_http_client_open(c, strlen(form)) == ESP_OK && esp_http_client_write(c, form, strlen(form)) >= 0) {
    esp_http_client_fetch_headers(c);
    status = esp_http_client_get_status_code(c);
    int n = esp_http_client_read_response(c, answer, cap - 1);
    answer[n > 0 ? n : 0] = 0;
  }
  esp_http_client_cleanup(c);
  return status;
}

esp_err_t h_pair_start(httpd_req_t *req) {
  if (!cb_status_authorize(req)) return ESP_OK;
  const CbProvisioning &p = cb_prov();
  char body[200], ip[20], id[8], code_in[40], flat_req[4], code[17];
  if (!read_body(req, body, sizeof body) || !cb::form_get(body, "ip", ip, sizeof ip) ||
      !cb::form_get(body, "id", id, sizeof id) || !cb::form_get(body, "code", code_in, sizeof code_in) ||
      !cb::form_get(body, "flat", flat_req, sizeof flat_req))
    return reply(req, "400 Bad Request", false, "Formular unvollständig");
  if (!cb::device_code_normalize(code_in, code))
    return reply(req, "400 Bad Request", false, "Gerätecode: 16 Zeichen vom Etikett der Inneneinheit");
  cb::UnitId flat = !strcmp(flat_req, "eg") ? cb::UnitId::EG : !strcmp(flat_req, "og") ? cb::UnitId::OG : cb::UnitId::None;
  if (flat == cb::UnitId::None) return reply(req, "400 Bad Request", false, "Wohnung unbekannt");
  if (p.has_key[(int)flat] && cb_mode() == cb::Mode::Normal)
    return reply(req, "403 Forbidden", false,
                 "Diese Wohnung ist schon gekoppelt - zum Ersetzen erst die Wartung der Außeneinheit öffnen");
  uint8_t key[32], nonce[cb::kPairNonceLen];
  cb::random_bytes(key, sizeof key);
  cb::random_bytes(nonce, sizeof nonce);
  cb::PairOffer o;
  if (!cb::pair_seal(code, p.serial, id, key, nonce, &o)) return reply(req, "500 Internal Server Error", false, "Kryptofehler");
  char n[33], e[65], t[65], form[300], answer[400];
  cb::to_hex_lower(o.nonce, sizeof o.nonce, n);
  cb::to_hex_lower(o.enc, 32, e);
  cb::to_hex_lower(o.tag, 32, t);
  snprintf(form, sizeof form, "au=%s&in=%s&n=%s&e=%s&t=%s", p.serial, id, n, e, t);
  int status = post_offer(ip, form, answer, sizeof answer);
  char msg[160], got_flat[8], ch[72];
  if (status != 200) {
    if (!cb::json_get_string(answer, "message", msg, sizeof msg))
      snprintf(msg, sizeof msg, "Inneneinheit antwortet nicht (HTTP %d)", status);
    cb_log("pairing: indoor unit %s refused (HTTP %d): %s", id, status, msg);
    return reply(req, "422 Unprocessable Entity", false, msg);
  }
  uint8_t c[32], expect[32];
  bool ok = cb::json_get_string(answer, "flat", got_flat, sizeof got_flat) && !strcmp(got_flat, flat_req) &&
            cb::json_get_string(answer, "c", ch, sizeof ch) && cb::from_hex(ch, c, 32) &&
            cb::pair_confirm(key, nonce, (uint8_t)flat, expect);
  // The confirmation proves the indoor unit really holds the key and serves this flat.
  if (!ok || memcmp(c, expect, 32) != 0) {
    memset(key, 0, sizeof key);
    return reply(req, "502 Bad Gateway", false, "Bestätigung der Inneneinheit ungültig - nicht gekoppelt");
  }
  bool saved = cb_prov_set_link(flat, key, id);
  memset(key, 0, sizeof key);
  if (!saved) return reply(req, "500 Internal Server Error", false, "Speichern fehlgeschlagen");
  cb_log("pairing: %s paired with indoor unit %s - restarting", cb::unit_name(flat), id);
  snprintf(msg, sizeof msg, "%s gekoppelt (Inneneinheit %s). Beide Einheiten starten neu.",
           cb::flat_key_label((uint8_t)flat), id);
  cb_setup_reboot_in(3000);
  return reply(req, "200 OK", true, msg);
}

esp_err_t h_pair_remove(httpd_req_t *req) {
  if (!cb_status_authorize(req)) return ESP_OK;
  if (cb_mode() == cb::Mode::Normal) return reply(req, "403 Forbidden", false, "nur in der Wartung");
  char body[64], f[4];
  if (!read_body(req, body, sizeof body) || !cb::form_get(body, "flat", f, sizeof f) || (strcmp(f, "eg") && strcmp(f, "og")))
    return reply(req, "400 Bad Request", false, "Wohnung fehlt");
  cb_prov_remove_link(!strcmp(f, "eg") ? cb::UnitId::EG : cb::UnitId::OG);
  cb_setup_reboot_in(2000);
  return reply(req, "200 OK", true, "Kopplung entfernt - Neustart");
}

esp_err_t h_pulse(httpd_req_t *req) {
  if (!cb_status_authorize(req)) return ESP_OK;
  if (cb_mode() == cb::Mode::Normal) return reply(req, "403 Forbidden", false, "nur in der Wartung");
  char body[64], v[8];
  if (!read_body(req, body, sizeof body) || !cb::form_get(body, "ms", v, sizeof v))
    return reply(req, "400 Bad Request", false, "Wert fehlt");
  int ms = atoi(v);
  if (!cb_door_set_pulse_ms(ms)) return reply(req, "400 Bad Request", false, "100 bis 5000 ms");
  char msg[64];
  snprintf(msg, sizeof msg, "Pulslänge %d ms gespeichert", ms);
  return reply(req, "200 OK", true, msg);
}
#endif

}  // namespace

void cb_pairing_begin() {}

void cb_pairing_register(httpd_handle_t s) {
#if CB_IS_OUTDOOR
  httpd_uri_t list = {"/pair/list", HTTP_GET, h_pair_list, nullptr};
  httpd_uri_t start = {"/pair/start", HTTP_POST, h_pair_start, nullptr};
  httpd_uri_t remove = {"/pair/remove", HTTP_POST, h_pair_remove, nullptr};
  httpd_uri_t pulse = {"/door/pulse", HTTP_POST, h_pulse, nullptr};
  httpd_register_uri_handler(s, &list);
  httpd_register_uri_handler(s, &start);
  httpd_register_uri_handler(s, &remove);
  httpd_register_uri_handler(s, &pulse);
#else
  httpd_uri_t accept = {"/pair", HTTP_POST, h_pair_accept, nullptr};
  httpd_uri_t maint = {"/outdoor-maint", HTTP_POST, h_outdoor_maint, nullptr};
  httpd_uri_t name = {"/flat/name", HTTP_POST, h_flat_name, nullptr};
  httpd_register_uri_handler(s, &accept);
  httpd_register_uri_handler(s, &maint);
  httpd_register_uri_handler(s, &name);
#endif
}

void cb_pairing_poll() {
  if (maint_request.exchange(false)) {  // indoor: the web task asked, the loop owns the protocol engine
    bool ok = cb_net_send_event(cb::UnitId::Outdoor, cb::EventCode::MaintenanceRequest, nullptr, 0);
    cb_log("pairing: maintenance request to the outdoor unit %s", ok ? "sent" : "FAILED (not reachable?)");
  }
#if !CB_IS_OUTDOOR
  // Flat name to the outdoor unit once it answers (FW-B-21). A change while one is on its way sets the flag again.
  const CbProvisioning &p = cb_prov();
  uint32_t now = millis();
  if (name_pending.load() && !name_in_flight && p.valid && p.has_key[(int)cb::UnitId::Outdoor] &&
      cb_net_peer_reachable(cb::UnitId::Outdoor) && (int32_t)(now - name_next_try) >= 0) {
    name_pending = false;
    const char *n = p.flat_name[(int)p.role];
    name_in_flight = cb_net_send_event(cb::UnitId::Outdoor, cb::EventCode::FlatName, (const uint8_t *)n, strlen(n));
    if (!name_in_flight) {
      name_pending = true;
      name_next_try = now + kNameRetryMs;
    }
  }
#endif
}

void cb_pairing_on_event_result(cb::UnitId, cb::EventCode code, bool ok) {
#if !CB_IS_OUTDOOR
  if (code != cb::EventCode::FlatName || !name_in_flight) return;
  name_in_flight = false;
  if (ok) {
    cb_log("pairing: flat name '%s' handed to the outdoor unit", cb_prov_flat_name(cb_prov().role));
  } else {
    name_pending = true;
    name_next_try = millis() + kNameRetryMs;
  }
#else
  (void)code, (void)ok;
#endif
}

void cb_pairing_json(cb::Json &j) {
  const CbProvisioning &p = cb_prov();
  j.key("pairing");
  j.begin_obj();
#if CB_IS_OUTDOOR
  j.kv("eg", p.has_key[(int)cb::UnitId::EG] ? p.peer_serial[(int)cb::UnitId::EG] : "");
  j.kv("eg_paired", p.has_key[(int)cb::UnitId::EG]);
  j.kv("og", p.has_key[(int)cb::UnitId::OG] ? p.peer_serial[(int)cb::UnitId::OG] : "");
  j.kv("og_paired", p.has_key[(int)cb::UnitId::OG]);
#else
  j.kv("outdoor", p.peer_serial[(int)cb::UnitId::Outdoor]);
  j.kv("paired", p.has_key[(int)cb::UnitId::Outdoor]);
  bool flat = p.role == cb::UnitId::EG || p.role == cb::UnitId::OG;
  j.kv("own_name", flat ? p.flat_name[(int)p.role] : "");  // what the buyer typed, "" = default shown
#endif
  j.end_obj();
}

void cb_pairing_on_event(cb::UnitId from, cb::EventCode code, const uint8_t *data, size_t len) {
#if CB_IS_OUTDOOR
  if (code == cb::EventCode::MaintenanceRequest) {
    bool ok = cb_modes_request_maintenance();
    cb_log("pairing: %s asked for maintenance - %s", cb::unit_name(from), ok ? "maintenance open for 10 min" : "refused (setup mode)");
  }
  if (code == cb::EventCode::FlatName && (from == cb::UnitId::EG || from == cb::UnitId::OG)) {
    // FW-B-21: the sender's link decides the flat; the text is checked like a typed one
    char raw[cb::kFlatNameMaxBytes + 1], name[cb::kFlatNameMaxBytes + 1];
    if (len > cb::kFlatNameMaxBytes) return;
    if (len) memcpy(raw, data, len);
    raw[len] = 0;
    if (!cb::flat_name_normalize(raw, name)) {
      cb_log("pairing: flat name from %s refused (not a valid name)", cb::unit_name(from));
      return;
    }
    bool changed = strcmp(cb_prov().flat_name[(int)from], name) != 0;
    if (!cb_prov_set_flat_name(from, name)) cb_log("pairing: flat name of %s NOT saved", cb::unit_name(from));
    else if (changed) cb_log("pairing: %s is now called '%s'", cb::unit_name(from), cb_prov_flat_name(from));
  }
#else
  (void)from, (void)code, (void)data, (void)len;
#endif
}
