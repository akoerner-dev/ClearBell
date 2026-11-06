// cb_phones.cpp - see cb_phones.h.
#include "cb_phones.h"

#include <Arduino.h>
#include <WiFi.h>

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

bool valid_id(const char *s) {
  size_t n = strlen(s);
  if (n == 0 || n > kIdLen) return false;
  for (const char *c = s; *c; c++)
    if (!isalnum((unsigned char)*c) && *c != '-') return false;
  return true;
}

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

// One-time download of the shortcut (RAM only).
struct Export {
  char token[33];
  char id[kIdLen + 1];
  char key[65];
  uint32_t until;
  int uses;
} exp_ = {};
portMUX_TYPE exp_lock = portMUX_INITIALIZER_UNLOCKED;

esp_err_t reply(httpd_req_t *req, const char *status, cb::Json &j) {
  httpd_resp_set_status(req, status);
  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");
  return httpd_resp_send(req, j.c_str(), j.len());
}

esp_err_t fail(httpd_req_t *req, const char *status, const char *msg) {
  char b[200];
  cb::Json j(b, sizeof b);
  j.begin_obj();
  j.kv("ok", false);
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

const char *precondition() {
  const CbProvisioning &p = cb_prov();
  if (p.role != cb::UnitId::EG) return "Fern-Öffnen gibt es nur für das EG";
  if (!p.has_key[(int)cb::UnitId::Outdoor]) return "erst mit der Außeneinheit koppeln";
  if (cb_mode() != cb::Mode::Maintenance) return "nur in der Wartung (Boot-Taster 5 s)";
  return nullptr;
}

void url_encode(const char *in, char *out, size_t cap) {
  size_t n = 0;
  for (; *in && n + 4 < cap; in++) {
    unsigned char c = (unsigned char)*in;
    if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') out[n++] = (char)c;
    else n += snprintf(out + n, cap - n, "%%%02X", c);
  }
  out[n] = 0;
}

esp_err_t h_add(httpd_req_t *req) {
  if (!cb_status_authorize(req)) return ESP_OK;
  const char *why = precondition();
  if (why) return fail(req, "403 Forbidden", why);
  char body[64], id[16];
  if (!read_body(req, body, sizeof body) || !cb::form_get(body, "name", id, sizeof id) || !valid_id(id))
    return fail(req, "400 Bad Request", "Name: 1–8 Zeichen, Buchstaben, Ziffern, Bindestrich");
  bool known = false;
  for (int i = 0; i < 4; i++) known |= !strcmp(cb_prov().phone_id[i], id);
  if (!known && cb_prov().phones >= 4) return fail(req, "409 Conflict", "schon 4 Handys - erst eines entfernen");

  uint8_t key[32], nonce[cb::kPairNonceLen] = {}, payload[47] = {};
  cb::random_bytes(key, sizeof key);
  cb::random_bytes(nonce, kNonceUsed);
  memcpy(payload, id, strlen(id));
  memcpy(payload + kIdLen, nonce, kNonceUsed);
  char key_hex[65];
  cb::to_hex_lower(key, 32, key_hex);  // the phone uses the 64 hex characters as its key text
  if (!cb::crypt32(cb_prov().key[(int)cb::UnitId::Outdoor], kLabel, nonce, key, 32, payload + kIdLen + kNonceUsed))
    return fail(req, "500 Internal Server Error", "Kryptofehler");
  memset(key, 0, sizeof key);
  if (!run_event(cb::EventCode::PhoneAdd, payload, sizeof payload))
    return fail(req, "504 Gateway Timeout", "Außeneinheit hat nicht bestätigt - erreichbar?");
  cb_prov_phone_store(id, "");  // indoor keeps only the name

  char token[33];
  uint8_t t[16];
  cb::random_bytes(t, sizeof t);
  cb::to_hex_lower(t, sizeof t, token);
  taskENTER_CRITICAL(&exp_lock);
  snprintf(exp_.token, sizeof exp_.token, "%s", token);
  snprintf(exp_.id, sizeof exp_.id, "%.8s", id);
  snprintf(exp_.key, sizeof exp_.key, "%s", key_hex);
  exp_.until = millis() + 10 * 60 * 1000;
  exp_.uses = 0;
  taskEXIT_CRITICAL(&exp_lock);
  memset(key_hex, 0, sizeof key_hex);

  char dl[96], dl_enc[160], imp[220], ntfy[120];
  snprintf(dl, sizeof dl, "http://%s/phone/%s.json", WiFi.localIP().toString().c_str(), token);
  url_encode(dl, dl_enc, sizeof dl_enc);
  snprintf(imp, sizeof imp, "https://http-shortcuts.rmy.ch/import?url=%s", dl_enc);
  snprintf(ntfy, sizeof ntfy, "ntfy://ntfy.sh/%s?display=ClearBell%%20EG", cb_prov().topic_out);
  cb_log("phones: phone '%s' added (outdoor confirmed), shortcut download open for 10 min", id);
  char b[700];
  cb::Json j(b, sizeof b);
  j.begin_obj();
  j.kv("ok", true);
  j.kv("message", "Handy eingetragen. Jetzt auf DIESEM Handy beide Knöpfe antippen (10 min gültig).");
  j.kv("ntfy", ntfy);
  j.kv("import", imp);
  j.kv("download", dl);
  j.end_obj();
  return reply(req, "200 OK", j);
}

esp_err_t h_remove(httpd_req_t *req) {
  if (!cb_status_authorize(req)) return ESP_OK;
  const char *why = precondition();
  if (why) return fail(req, "403 Forbidden", why);
  char body[64], id[16];
  if (!read_body(req, body, sizeof body) || !cb::form_get(body, "name", id, sizeof id) || !valid_id(id))
    return fail(req, "400 Bad Request", "Name fehlt");
  uint8_t payload[kIdLen] = {};
  memcpy(payload, id, strlen(id));
  if (!run_event(cb::EventCode::PhoneRemove, payload, sizeof payload))
    return fail(req, "504 Gateway Timeout", "Außeneinheit hat nicht bestätigt - erreichbar?");
  cb_prov_phone_forget(id);
  cb_log("phones: phone '%s' removed - its signatures are refused from now on", id);
  char b[200];
  cb::Json j(b, sizeof b);
  j.begin_obj();
  j.kv("ok", true);
  j.kv("message", "Handy entfernt - es kann die Tür nicht mehr öffnen.");
  j.end_obj();
  return reply(req, "200 OK", j);
}

// GET /phone/<token>.json - no login: the token is the secret. Format: HTTP Shortcuts export, version 92 (plain
// JSON is accepted by its importer). The ids are fixed so a second import replaces instead of duplicating.
esp_err_t h_download(httpd_req_t *req) {
  const char *t = req->uri + strlen("/phone/");
  Export e;
  taskENTER_CRITICAL(&exp_lock);
  bool ok = exp_.token[0] && strlen(t) == 37 && !strncmp(t, exp_.token, 32) && !strcmp(t + 32, ".json") &&
            (int32_t)(exp_.until - millis()) > 0 && exp_.uses < 3;
  if (ok) {
    exp_.uses++;
    e = exp_;
  }
  taskEXIT_CRITICAL(&exp_lock);
  if (!ok) {
    httpd_resp_set_status(req, "404 Not Found");
    return httpd_resp_sendstr(req, "abgelaufen - Handy auf der Seite der Inneneinheit neu hinzufügen\n");
  }
  const CbProvisioning &p = cb_prov();
  char url[96], code[400];
  snprintf(url, sizeof url, "https://ntfy.sh/%s", p.topic_in);
  // Runs on the phone right before the request: checks the token from the push and signs it (cb_remote_logic.h).
  snprintf(code, sizeof code,
           "const t = getVariable('token');\n"
           "if (!/^[0-9a-f]{32}$/.test(t)) { showToast('Bitte über die Klingel-Benachrichtigung starten'); abort(); }\n"
           "setVariable('cbbody', 'CBR1 ' + t + ' %s ' + toHexString(hmac('SHA-256', '%s', 'CBR1|' + t + '|%s')));",
           e.id, e.key, e.id);
  static const char *kTokenVar = "6f1c2a3e-8b4d-4c5e-9f60-7a8b9c0d1e2f";
  static const char *kBodyVar = "7a2d3b4f-9c5e-4d6f-a071-8b9c0d1e2f30";
  char body_ph[48];
  snprintf(body_ph, sizeof body_ph, "{{%s}}", kBodyVar);
  static char b[2048];
  cb::Json j(b, sizeof b);
  j.begin_obj();
  j.kv("version", 92);
  j.kv("compatibilityVersion", 90);
  j.key("categories");
  j.begin_arr();
  j.begin_obj();
  j.kv("id", "3c4e5f60-7182-4394-a5b6-c7d8e9f0a1b2");
  j.kv("name", "ClearBell");
  j.key("shortcuts");
  j.begin_arr();
  j.begin_obj();
  j.kv("id", p.shortcut_id);  // the push button opens http-shortcuts://<this id>?token=...
  j.kv("name", "ClearBell Tür öffnen");
  j.kv("method", "POST");
  j.kv("url", url);
  j.kv("bodyContent", body_ph);
  j.kv("requestBodyType", "custom_text");
  j.kv("contentType", "text/plain");
  j.kv("codeOnPrepare", code);
  j.end_obj();
  j.end_arr();
  j.end_obj();
  j.end_arr();
  j.key("variables");
  j.begin_arr();
  j.begin_obj();
  j.kv("id", kTokenVar);
  j.kv("key", "token");
  j.kv("type", "constant");
  j.kv("value", "");
  j.end_obj();
  j.begin_obj();
  j.kv("id", kBodyVar);
  j.kv("key", "cbbody");
  j.kv("type", "constant");
  j.kv("value", "");
  j.end_obj();
  j.end_arr();
  j.end_obj();
  memset(code, 0, sizeof code);
  if (!j.ok()) return httpd_resp_send_500(req);
  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");
  httpd_resp_set_hdr(req, "Content-Disposition", "attachment; filename=\"clearbell.json\"");
  cb_log("phones: shortcut for '%s' downloaded (%d of 3)", e.id, e.uses);
  esp_err_t r = httpd_resp_send(req, j.c_str(), j.len());
  memset(b, 0, sizeof b);
  memset(&e, 0, sizeof e);
  return r;
}
#endif

}  // namespace

void cb_phones_register(httpd_handle_t s) {
#if !CB_IS_OUTDOOR
  httpd_uri_t add = {"/phone/add", HTTP_POST, h_add, nullptr};
  httpd_uri_t rem = {"/phone/remove", HTTP_POST, h_remove, nullptr};
  httpd_uri_t dl = {"/phone/*", HTTP_GET, h_download, nullptr};
  httpd_register_uri_handler(s, &add);
  httpd_register_uri_handler(s, &rem);
  httpd_register_uri_handler(s, &dl);
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
  if (from != cb::UnitId::EG) {
    cb_log("phones: phone event from %s ignored - remote opening is EG only", cb::unit_name(from));
    return;
  }
  char id[kIdLen + 1] = {};
  if (len < kIdLen) return;
  memcpy(id, data, kIdLen);
  if (!valid_id(id)) return;
  if (code == cb::EventCode::PhoneRemove) {
    cb_prov_phone_forget(id);
    cb_remote_phone_remove(id);
    cb_log("phones: phone '%s' removed by the EG unit", id);
    return;
  }
  if (len != kIdLen + kNonceUsed + 32) return;
  uint8_t nonce[cb::kPairNonceLen] = {}, key[32];
  memcpy(nonce, data + kIdLen, kNonceUsed);
  char key_hex[65];
  if (!cb::crypt32(cb_prov().key[(int)cb::UnitId::EG], kLabel, nonce, data + kIdLen + kNonceUsed, 32, key)) return;
  cb::to_hex_lower(key, 32, key_hex);
  memset(key, 0, sizeof key);
  int slot = cb_prov_phone_store(id, key_hex);
  bool live = slot >= 0 && cb_remote_phone_add(id, key_hex);
  memset(key_hex, 0, sizeof key_hex);
  cb_log("phones: phone '%s' added by the EG unit (slot %d, %s)", id, slot, live ? "active" : "NOT active");
#else
  (void)from, (void)code, (void)data, (void)len;
#endif
}

void cb_phones_json(cb::Json &j) {
  const CbProvisioning &p = cb_prov();
  j.key("phones");
  j.begin_obj();
  j.kv("available", p.role == cb::UnitId::EG && p.has_key[(int)cb::UnitId::Outdoor] && !CB_IS_OUTDOOR);
  j.key("list");
  j.begin_arr();
  for (int i = 0; i < 4; i++)
    if (p.phone_id[i][0]) j.str(p.phone_id[i]);
  j.end_arr();
  j.end_obj();
}
