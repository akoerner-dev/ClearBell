// cb_phones.cpp - see cb_phones.h.
#include "cb_phones.h"

#include <Arduino.h>

#include <atomic>
#include <cstring>

#include "cb_config.h"
#include "cb_crypto.h"
#include "cb_log.h"
#include "cb_modes.h"
#include "cb_net.h"
#include "cb_pairing_logic.h"
#include "cb_provisioning.h"
#include "cb_remote.h"
#include "cb_remote_logic.h"
#include "cb_setup_logic.h"
#include "cb_status.h"

namespace {

constexpr const char *kLabel = "phone";
constexpr size_t kIdLen = 8, kNonceUsed = 7;  // event payload: id 8 + nonce 7 + key 32 = 47 = kMaxPayload - 1

#if !CB_IS_OUTDOOR
// ---- Indoor: web -> loop hand-over of one event at a time ---------------------------------------------------
enum State : int { kIdle, kPreparing, kQueued, kSent, kOk, kFailed };
std::atomic<int> state{kIdle};
cb::EventCode pending_code;
uint8_t pending_data[47];
size_t pending_len = 0;

bool run_event(cb::EventCode code, const uint8_t *data, size_t len) {
  int idle = kIdle;
  if (!state.compare_exchange_strong(idle, kPreparing)) return false;  // one at a time; loop waits for kQueued
  pending_code = code;
  memcpy(pending_data, data, len);
  pending_len = len;
  state = kQueued;
  for (int i = 0; i < 50; i++) {  // the event needs < 1.2 s incl. retries
    int s = state.load();
    if (s == kOk || s == kFailed) {
      state = kIdle;
      return s == kOk;
    }
    vTaskDelay(pdMS_TO_TICKS(100));
  }
  state = kIdle;
  return false;
}

esp_err_t reply(httpd_req_t *req, const char *status, cb::Json &j) {
  httpd_resp_set_status(req, status);
  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");
  return httpd_resp_send(req, j.c_str(), j.len());
}

esp_err_t answer(httpd_req_t *req, const char *status, bool ok, const char *msg) {
  char b[240];
  cb::Json j(b, sizeof b);
  j.begin_obj();
  j.kv("ok", ok);
  j.kv("message", msg);
  j.end_obj();
  return reply(req, status, j);
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

uint8_t own_flat() { return (uint8_t)cb_prov().role; }

const char *precondition() {
  const CbProvisioning &p = cb_prov();
  if (p.role != cb::UnitId::EG && p.role != cb::UnitId::OG) return "Wohnung nicht eingerichtet";
  if (!p.has_key[(int)cb::UnitId::Outdoor]) return "erst mit der Außeneinheit koppeln";
  if (cb_mode() != cb::Mode::Maintenance) return "nur in der Wartung (Boot-Taster 5 s)";
  return nullptr;
}

int find_phone(const char *id) {
  const CbProvisioning &p = cb_prov();
  for (int i = 0; i < CbProvisioning::kPhoneSlots; i++)
    if (p.phone_id[i][0] && p.phone_flat[i] == own_flat() && !strcmp(p.phone_id[i], id)) return i;
  return -1;
}

// The one link a phone needs (R27): subscribe its push topic in the ntfy app. Pushes carry the door button.
esp_err_t link_reply(httpd_req_t *req, const char *msg, const char *id, const char *key_hex) {
  char push[cb::kTopicLen + 1], cmd[cb::kTopicLen + 1];
  if (!cb::phone_topics(key_hex, push, cmd))
    return answer(req, "409 Conflict", false, "Für dieses Handy fehlt der Schlüssel – bitte entfernen und neu hinzufügen.");
  char ntfy[220], web[80], display[64], enc[160];
  // FW-B-21: the subscription carries the flat's own name ("ClearBell Familie Müller")
  snprintf(display, sizeof display, "ClearBell %s", cb_prov_flat_name(cb_prov().role));
  if (!cb::url_encode(display, enc, sizeof enc)) snprintf(enc, sizeof enc, "ClearBell");
  snprintf(ntfy, sizeof ntfy, "ntfy://ntfy.sh/%s?display=%s", push, enc);
  snprintf(web, sizeof web, "https://ntfy.sh/%s", push);
  char b[600];
  cb::Json j(b, sizeof b);
  j.begin_obj();
  j.kv("ok", true);
  j.kv("message", msg);
  j.kv("id", id);  // FW-B-23: the page offers "Test" for exactly this phone next to the link
  j.kv("ntfy", ntfy);
  j.kv("web", web);
  j.end_obj();
  return reply(req, "200 OK", j);
}

bool read_name(httpd_req_t *req, char id[16]) {
  char body[64];
  return read_body(req, body, sizeof body) && cb::form_get(body, "name", id, 16) && cb::valid_phone_id(id);
}

esp_err_t h_add(httpd_req_t *req) {
  if (!cb_status_authorize(req)) return ESP_OK;
  const char *why = precondition();
  if (why) return answer(req, "403 Forbidden", false, why);
  char id[16];
  if (!read_name(req, id)) return answer(req, "400 Bad Request", false, "Name: 1–8 Zeichen, Buchstaben, Ziffern, Bindestrich");
  int n = 0;
  for (int i = 0; i < CbProvisioning::kPhoneSlots; i++) n += cb_prov().phone_id[i][0] && cb_prov().phone_flat[i] == own_flat();
  if (find_phone(id) < 0 && n >= cb::kPhonesPerFlat)
    return answer(req, "409 Conflict", false, "schon 4 Handys – erst eines entfernen");

  uint8_t key[32], nonce[cb::kPairNonceLen] = {}, payload[47] = {};
  cb::random_bytes(key, sizeof key);
  cb::random_bytes(nonce, kNonceUsed);
  memcpy(payload, id, strlen(id));
  memcpy(payload + kIdLen, nonce, kNonceUsed);
  char key_hex[65];
  cb::to_hex_lower(key, 32, key_hex);  // the 64 hex characters are the phone key (topics: cb::phone_topics)
  if (!cb::crypt32(cb_prov().key[(int)cb::UnitId::Outdoor], kLabel, nonce, key, 32, payload + kIdLen + kNonceUsed))
    return answer(req, "500 Internal Server Error", false, "Kryptofehler");
  memset(key, 0, sizeof key);
  if (!run_event(cb::EventCode::PhoneAdd, payload, sizeof payload))
    return answer(req, "504 Gateway Timeout", false, "Außeneinheit hat nicht bestätigt – erreichbar?");
  cb_prov_phone_store(id, key_hex, own_flat());  // indoor keeps the key too: outage notice, link again (R27)
  cb_log("phones: phone '%s' added (outdoor confirmed)", id);
  esp_err_t r = link_reply(req, "Handy eingetragen. Jetzt auf DIESEM Handy den Link öffnen (App „ntfy“).", id, key_hex);
  memset(key_hex, 0, sizeof key_hex);
  return r;
}

esp_err_t h_link(httpd_req_t *req) {
  if (!cb_status_authorize(req)) return ESP_OK;
  const char *why = precondition();
  if (why) return answer(req, "403 Forbidden", false, why);
  char id[16];
  int slot = read_name(req, id) ? find_phone(id) : -1;
  if (slot < 0) return answer(req, "404 Not Found", false, "Handy nicht gefunden");
  return link_reply(req, "Link für dieses Handy – auf dem Handy öffnen.", id, cb_prov().phone_key[slot]);
}

esp_err_t h_test(httpd_req_t *req) {
  if (!cb_status_authorize(req)) return ESP_OK;
  char id[16];
  int slot = read_name(req, id) ? find_phone(id) : -1;
  if (slot < 0) return answer(req, "404 Not Found", false, "Handy nicht gefunden");
  if (!cb_prov().phone_key[slot][0])
    return answer(req, "409 Conflict", false, "Für dieses Handy fehlt der Schlüssel – bitte entfernen und neu hinzufügen.");
  cb_remote_welcome(cb_prov().phone_key[slot]);
  return answer(req, "200 OK", true, "Test-Nachricht ist unterwegs – kommt sie auf dem Handy an?");
}

esp_err_t h_remove(httpd_req_t *req) {
  if (!cb_status_authorize(req)) return ESP_OK;
  const char *why = precondition();
  if (why) return answer(req, "403 Forbidden", false, why);
  char id[16];
  if (!read_name(req, id)) return answer(req, "400 Bad Request", false, "Name fehlt");
  uint8_t payload[kIdLen] = {};
  memcpy(payload, id, strlen(id));
  if (!run_event(cb::EventCode::PhoneRemove, payload, sizeof payload))
    return answer(req, "504 Gateway Timeout", false, "Außeneinheit hat nicht bestätigt – erreichbar?");
  cb_prov_phone_forget(id, own_flat());
  cb_log("phones: phone '%s' removed - its topics are dropped from now on", id);
  return answer(req, "200 OK", true, "Handy entfernt – es bekommt keine Nachrichten mehr und kann die Tür nicht öffnen.");
}
#endif

}  // namespace

void cb_phones_register(httpd_handle_t s) {
#if !CB_IS_OUTDOOR
  httpd_uri_t add = {"/phone/add", HTTP_POST, h_add, nullptr};
  httpd_uri_t rem = {"/phone/remove", HTTP_POST, h_remove, nullptr};
  httpd_uri_t link = {"/phone/link", HTTP_POST, h_link, nullptr};
  httpd_uri_t test = {"/phone/test", HTTP_POST, h_test, nullptr};
  httpd_register_uri_handler(s, &add);
  httpd_register_uri_handler(s, &rem);
  httpd_register_uri_handler(s, &link);
  httpd_register_uri_handler(s, &test);
#else
  (void)s;
#endif
}

void cb_phones_poll() {
#if !CB_IS_OUTDOOR
  if (state.load() != kQueued) return;
  state = kSent;
  if (!cb_net_send_event(cb::UnitId::Outdoor, pending_code, pending_data, pending_len)) state = kFailed;
  memset(pending_data, 0, sizeof pending_data);
#endif
}

void cb_phones_on_event_result(cb::UnitId, cb::EventCode code, bool ok) {
#if !CB_IS_OUTDOOR
  if ((code == cb::EventCode::PhoneAdd || code == cb::EventCode::PhoneRemove) && state.load() == kSent)
    state = ok ? kOk : kFailed;
#else
  (void)code, (void)ok;
#endif
}

void cb_phones_on_event(cb::UnitId from, cb::EventCode code, const uint8_t *data, size_t len) {
#if CB_IS_OUTDOOR
  if (code != cb::EventCode::PhoneAdd && code != cb::EventCode::PhoneRemove) return;
  if (from != cb::UnitId::EG && from != cb::UnitId::OG) return;
  uint8_t flat = (uint8_t)from;  // R27: each flat brings its own phones; the sender's link decides the flat
  char id[kIdLen + 1] = {};
  if (len < kIdLen) return;
  memcpy(id, data, kIdLen);
  if (!cb::valid_phone_id(id)) return;
  if (code == cb::EventCode::PhoneRemove) {
    cb_prov_phone_forget(id, flat);
    cb_remote_phone_remove(id, flat);
    cb_log("phones: phone '%s' removed by the %s unit", id, cb::unit_name(from));
    return;
  }
  if (len != kIdLen + kNonceUsed + 32) return;
  uint8_t nonce[cb::kPairNonceLen] = {}, key[32];
  memcpy(nonce, data + kIdLen, kNonceUsed);
  char key_hex[65];
  if (!cb::crypt32(cb_prov().key[(int)from], kLabel, nonce, data + kIdLen + kNonceUsed, 32, key)) return;
  cb::to_hex_lower(key, 32, key_hex);
  memset(key, 0, sizeof key);
  int slot = cb_prov_phone_store(id, key_hex, flat);
  bool live = slot >= 0 && cb_remote_phone_add(id, key_hex, flat);
  memset(key_hex, 0, sizeof key_hex);
  cb_log("phones: phone '%s' added by the %s unit (slot %d, %s)", id, cb::unit_name(from), slot,
         live ? "active" : "NOT active");
#else
  (void)from, (void)code, (void)data, (void)len;
#endif
}

void cb_phones_json(cb::Json &j) {
  const CbProvisioning &p = cb_prov();
  j.key("phones");
  j.begin_obj();
  j.kv("available", !CB_IS_OUTDOOR && (p.role == cb::UnitId::EG || p.role == cb::UnitId::OG) &&
                        p.has_key[(int)cb::UnitId::Outdoor]);
  j.key("list");
  j.begin_arr();
  for (int i = 0; i < CbProvisioning::kPhoneSlots; i++)
    if (p.phone_id[i][0] && (CB_IS_OUTDOOR || p.phone_flat[i] == (uint8_t)p.role)) j.str(p.phone_id[i]);
  j.end_arr();
  j.key("nokey");  // indoor entries from before R27: must be added again to get pushes
  j.begin_arr();
  for (int i = 0; i < CbProvisioning::kPhoneSlots; i++)
    if (p.phone_id[i][0] && !p.phone_key[i][0]) j.str(p.phone_id[i]);
  j.end_arr();
  j.end_obj();
}
