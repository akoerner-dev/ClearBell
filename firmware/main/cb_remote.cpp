// cb_remote.cpp - see cb_remote.h.
#include "cb_remote.h"

#include "cb_config.h"

#if CB_IS_OUTDOOR

#include <Arduino.h>
#include <esp_crt_bundle.h>
#include <esp_http_client.h>

#include <atomic>
#include <cstring>

#include "cb_console.h"
#include "cb_door.h"
#include "cb_door_logic.h"
#include "cb_log.h"
#include "cb_modes.h"
#include "cb_net.h"
#include "cb_provisioning.h"
#include "cb_remote_logic.h"
#include "cb_status.h"
#include "cb_system.h"

namespace {

constexpr const char *kServer = "https://ntfy.sh";
constexpr uint32_t kRingPushGapMs = 30000;   // a child pressing ten times sends one push, not ten
constexpr uint32_t kPushMaxAgeMs = 60000;    // a ring push that could not go out within 60 s is dropped
constexpr uint32_t kStreamSilentMs = 120000; // ntfy sends a keepalive every 45 s; nothing for 2 min = dead
constexpr size_t kMsgMax = 200;              // a command is ~140 characters

enum class PushKind : uint8_t { Ring, Opened, Refused, Restart, Test };
struct Push {
  PushKind kind;
  uint32_t queued_ms;
  char text[96];   // token (Ring), phone id (Opened), reason (Refused/Restart)
};
struct Cmd {
  char msg[kMsgMax + 1];
};

bool enabled = false;
QueueHandle_t push_q = nullptr, cmd_q = nullptr;
cb::RemoteAuth auth;  // owned by loop()
uint32_t last_ring_push = 0;
bool any_ring_push = false;

// Counters (written by one task each, only displayed elsewhere)
std::atomic<uint32_t> pushes_ok{0}, pushes_failed{0}, stream_connects{0}, stream_errors{0}, cmds_rx{0};
std::atomic<uint32_t> rx_unreadable{0};  // ntfy messages without readable text (too long) - written by ntfy_rx
std::atomic<uint32_t> last_stream_rx_ms{0};
std::atomic<bool> stream_up{false};
uint32_t cmd_result[(int)cb::RemoteResult::BadSignature + 1] = {};
uint32_t opened = 0, refused_lockout = 0;
char last_id[24] = "";  // id of the last ntfy message seen: a reconnect continues from there (?since=)

// ---- Push (task ntfy_tx) ------------------------------------------------------------------------------------

bool publish(const Push &p) {
  const CbProvisioning &pv = cb_prov();
  static char body[768];
  cb::Json j(body, sizeof body);
  j.begin_obj();
  j.kv("topic", pv.topic_out);
  switch (p.kind) {
    case PushKind::Ring: {
      j.kv("title", "Es klingelt – EG");
      j.kv("message", "Tür öffnen nur, wenn du weißt, wer davor steht. Gültig 5 min.");
      j.kv("priority", 4);
      j.key("tags");
      j.begin_arr();
      j.str("bell");
      j.end_arr();
      if (pv.fern_link[0]) {
        // Link template from provisioning with "{token}" replaced (the app's shortcut id is in the template).
        char url[200];
        const char *ph = strstr(pv.fern_link, "{token}");
        if (ph) snprintf(url, sizeof url, "%.*s%s%s", (int)(ph - pv.fern_link), pv.fern_link, p.text, ph + 7);
        else snprintf(url, sizeof url, "%s", pv.fern_link);
        j.key("actions");
        j.begin_arr();
        j.begin_obj();
        j.kv("action", "view");
        j.kv("label", "Tür öffnen");
        j.kv("url", url);
        j.kv("clear", true);
        j.end_obj();
        j.end_arr();
      }
      break;
    }
    case PushKind::Opened: {
      char m[160];
      snprintf(m, sizeof m, "Die Haustür wurde per App geöffnet (Handy %.8s).", p.text);
      j.kv("title", "Tür per App geöffnet");
      j.kv("message", m);
      j.key("tags");
      j.begin_arr();
      j.str("door");
      j.end_arr();
      break;
    }
    case PushKind::Refused:
      j.kv("title", "Tür NICHT geöffnet");
      j.kv("message", p.text);
      break;
    case PushKind::Restart: {
      char m[160];
      snprintf(m, sizeof m, "%.15s ist neu gestartet. Grund: %.95s", pv.name, p.text);
      j.kv("title", "ClearBell neu gestartet");
      j.kv("message", m);
      break;
    }
    case PushKind::Test:
      j.kv("title", "ClearBell Test");
      j.kv("message", "Testnachricht der Außeneinheit (Konsole remote-push).");
      break;
  }
  j.end_obj();
  if (!j.ok()) return false;

  esp_http_client_config_t cfg = {};
  cfg.url = kServer;
  cfg.method = HTTP_METHOD_POST;
  cfg.timeout_ms = 10000;
  cfg.crt_bundle_attach = esp_crt_bundle_attach;  // certificate check against the Mozilla bundle (S-1)
  esp_http_client_handle_t c = esp_http_client_init(&cfg);
  if (!c) return false;
  esp_http_client_set_header(c, "Content-Type", "application/json");
  esp_http_client_set_post_field(c, body, (int)j.len());
  esp_err_t e = esp_http_client_perform(c);
  int status = e == ESP_OK ? esp_http_client_get_status_code(c) : 0;
  esp_http_client_cleanup(c);
  if (status != 200) cb_log("remote: push failed (%s, HTTP %d)", esp_err_to_name(e), status);
  return status == 200;
}

void tx_task(void *) {
  Push p;
  for (;;) {
    if (xQueueReceive(push_q, &p, portMAX_DELAY) != pdTRUE) continue;
    bool ok = false;
    // The restart notice waits for WiFi as long as it takes; everything else is only useful for a minute.
    auto too_old = [&p] { return p.kind != PushKind::Restart && millis() - p.queued_ms > kPushMaxAgeMs; };
    for (int attempt = 0; attempt < 4 && !ok && !too_old(); attempt++) {
      while (!cb_net_wifi_up() && !too_old()) vTaskDelay(pdMS_TO_TICKS(500));
      if (too_old()) break;
      ok = publish(p);
      if (!ok) vTaskDelay(pdMS_TO_TICKS(2000 * (attempt + 1)));
    }
    if (ok) pushes_ok++;
    else pushes_failed++;
  }
}

void queue_push(PushKind k, const char *text) {
  if (!push_q) return;
  Push p{k, millis(), {}};
  snprintf(p.text, sizeof p.text, "%s", text ? text : "");
  if (xQueueSend(push_q, &p, 0) != pdTRUE) pushes_failed++;
}

// ---- Command stream (task ntfy_rx) ----------------------------------------------------------------------------

void handle_line(const char *line) {
  char v[kMsgMax + 1];
  last_stream_rx_ms = millis();
  if (!cb::json_get_string(line, "event", v, sizeof v)) return;
  if (strcmp(v, "message") != 0) return;  // open, keepalive, poll_request
  char id[sizeof last_id];
  if (cb::json_get_string(line, "id", id, sizeof id)) memcpy(last_id, id, sizeof last_id);
  Cmd c{};
  cmds_rx++;  // every message on the command topic, readable or not
  if (!cb::json_get_string(line, "message", c.msg, sizeof c.msg)) {
    rx_unreadable++;  // too long or no text: cannot be ours
    return;
  }
  xQueueSend(cmd_q, &c, 0);  // full queue = flood: dropped, the loop checks at its own pace
}

void stream_once() {
  char url[160];
  snprintf(url, sizeof url, "%s/%s/json%s%s", kServer, cb_prov().topic_in, last_id[0] ? "?since=" : "", last_id);
  esp_http_client_config_t cfg = {};
  cfg.url = url;
  cfg.timeout_ms = 1000;  // per read: returns what arrived, so a line is handled at once (not when 1 KB is full)
  cfg.buffer_size = 1024;
  cfg.crt_bundle_attach = esp_crt_bundle_attach;
  esp_http_client_handle_t c = esp_http_client_init(&cfg);
  if (!c) return;
  if (esp_http_client_open(c, 0) != ESP_OK) {
    stream_errors++;
    esp_http_client_cleanup(c);
    return;
  }
  esp_http_client_fetch_headers(c);
  int status = esp_http_client_get_status_code(c);
  if (status != 200) {
    cb_log("remote: command stream refused (HTTP %d)", status);
    stream_errors++;
    esp_http_client_close(c);
    esp_http_client_cleanup(c);
    return;
  }
  stream_connects++;
  stream_up = true;
  last_stream_rx_ms = millis();
  static char line[1024];
  size_t len = 0;
  char buf[256];
  while (cb_net_wifi_up() && millis() - last_stream_rx_ms < kStreamSilentMs) {
    int n = esp_http_client_read(c, buf, sizeof buf);
    if (n == -ESP_ERR_HTTP_EAGAIN || n == 0) {
      if (esp_http_client_is_complete_data_received(c)) break;  // server closed the stream
      continue;
    }
    if (n < 0) break;
    for (int i = 0; i < n; i++) {
      if (buf[i] == '\n') {
        line[len] = 0;
        if (len) handle_line(line);
        len = 0;
      } else if (len + 1 < sizeof line) {
        line[len++] = buf[i];
      } else {
        len = 0;  // overlong line: not a ntfy message of ours, drop it
      }
    }
  }
  stream_up = false;
  esp_http_client_close(c);
  esp_http_client_cleanup(c);
}

void rx_task(void *) {
  uint32_t backoff = 2000;
  for (;;) {
    if (!cb_net_wifi_up()) {
      vTaskDelay(pdMS_TO_TICKS(1000));
      continue;
    }
    uint32_t t0 = millis();
    stream_once();
    // A stream that ran for a while was fine: reconnect quickly. Quick failures back off up to 60 s.
    backoff = millis() - t0 > 60000 ? 2000 : (backoff * 2 > 60000 ? 60000 : backoff * 2);
    vTaskDelay(pdMS_TO_TICKS(backoff));
  }
}

// ---- Console -------------------------------------------------------------------------------------------------

void cmd_remote(const char *) {
  if (!enabled) {
    cb_log("remote: OFF (EG not paired)");
    return;
  }
  uint32_t now = millis();
  cb_log("remote: %d phone(s), stream %s (connects %lu, errors %lu, last data %lu s ago), tokens active %d",
         auth.phone_count(), stream_up ? "UP" : "down", (unsigned long)stream_connects.load(),
         (unsigned long)stream_errors.load(), (unsigned long)((now - last_stream_rx_ms) / 1000), auth.active(now));
  cb_log("remote: pushes ok %lu failed %lu; commands %lu: ok %lu, malformed %lu, scheme %lu, phone %lu, token %lu, "
         "expired %lu, BAD SIGNATURE %lu; opened %lu, refused (lock-out) %lu",
         (unsigned long)pushes_ok.load(), (unsigned long)pushes_failed.load(), (unsigned long)cmds_rx.load(),
         (unsigned long)cmd_result[0], (unsigned long)cmd_result[1], (unsigned long)cmd_result[2],
         (unsigned long)cmd_result[3], (unsigned long)cmd_result[4], (unsigned long)cmd_result[5],
         (unsigned long)cmd_result[6], (unsigned long)opened, (unsigned long)refused_lockout);
}

void cmd_remote_push(const char *) {
  if (!enabled) return cmd_remote(nullptr);
  queue_push(PushKind::Test, nullptr);
  cb_log("remote: test push queued");
}

}  // namespace

void cb_remote_begin() {
  const CbProvisioning &pv = cb_prov();
  cb_console_add("remote", "remote opening: stream, pushes, command counters", cmd_remote);
  cb_console_add("remote-push", "send a test push to the phones", cmd_remote_push);
  if (!pv.valid || !pv.topic_out[0] || !pv.topic_in[0]) {
    cb_log("remote: off (EG not paired - the names are derived from the EG pairing)");
    return;
  }
  for (int i = 0; i < 4; i++)
    if (pv.phone_id[i][0] && !auth.set_phone(i, pv.phone_id[i], pv.phone_key[i]))
      cb_log("remote: phone %d has an invalid key - ignored", i);
  push_q = xQueueCreate(6, sizeof(Push));
  cmd_q = xQueueCreate(4, sizeof(Cmd));
  // Core 0 next to WiFi, low priority: TLS handshakes take a second of CPU and must never delay door or audio.
  xTaskCreatePinnedToCore(tx_task, "ntfy_tx", 8192, nullptr, 1, nullptr, 0);
  xTaskCreatePinnedToCore(rx_task, "ntfy_rx", 8192, nullptr, 1, nullptr, 0);
  enabled = true;
  queue_push(PushKind::Restart, cb_system_reset_reason());  // SA 7.5: every start is announced
  cb_log("remote: on, %d phone(s)%s", auth.phone_count(), pv.fern_link[0] ? "" : ", no app link (push without button)");
}

bool cb_remote_phone_add(const char *id, const char *key_hex) {
  return enabled && auth.add_phone(id, key_hex) >= 0;
}

bool cb_remote_phone_remove(const char *id) {
  return auth.remove_phone(id);
}

void cb_remote_on_ring(cb::UnitId flat) {
  if (!enabled || flat != cb::UnitId::EG || auth.phone_count() == 0) return;
  uint32_t now = millis();
  if (any_ring_push && now - last_ring_push < kRingPushGapMs) return;
  any_ring_push = true;
  last_ring_push = now;
  char token[33];
  auth.issue(now, token);
  queue_push(PushKind::Ring, token);
}

void cb_remote_poll() {
  if (!enabled) return;
  Cmd c;
  if (xQueueReceive(cmd_q, &c, 0) != pdTRUE) return;  // one per pass: a flood cannot starve the loop
  char phone[cb::kPhoneIdMax + 1];
  cb::RemoteResult r = auth.check(c.msg, millis(), phone);
  cmd_result[(int)r]++;
  if (r != cb::RemoteResult::Ok) {
    // No push for refused commands: whoever can write to the topic could otherwise flood the phones.
    if (r == cb::RemoteResult::BadSignature) cb_log("remote: command with BAD SIGNATURE - ignored");
    else cb_log("remote: command ignored (%s)", cb::remote_result_name(r));
    return;
  }
  if (cb_mode() == cb::Mode::Setup) {
    queue_push(PushKind::Refused, "Die Außeneinheit ist in der Einrichtung.");
    return;
  }
  char who[16];
  snprintf(who, sizeof who, "app %s", phone);
  if (!cb_door_open(who, cb_door_pulse_ms())) {
    refused_lockout++;
    cb_log("remote: valid command from %s refused - lock-out time running (token stays valid)", phone);
    queue_push(PushKind::Refused, "Sperrzeit läuft - in ein paar Sekunden nochmal tippen.");
    return;
  }
  auth.consume_all();  // opened: no token of this ring (or the two before) works again
  opened++;
  cb_log("remote: door opened by phone %s", phone);
  cb_status_door_event(cb::DoorEvent::Remote);
  queue_push(PushKind::Opened, phone);
}

void cb_remote_json(cb::Json &j) {
  j.key("remote");
  j.begin_obj();
  j.kv("enabled", enabled);
  if (enabled) {
    uint32_t now = millis();
    j.kv("phones", auth.phone_count());
    j.kv("stream_up", stream_up.load());
    j.kv("last_data_s", (unsigned long)((now - last_stream_rx_ms) / 1000));
    j.kv("tokens_active", auth.active(now));
    j.kv("pushes_ok", (unsigned long)pushes_ok.load());
    j.kv("pushes_failed", (unsigned long)pushes_failed.load());
    j.kv("commands", (unsigned long)cmds_rx.load());
    j.kv("bad_signature", (unsigned long)cmd_result[(int)cb::RemoteResult::BadSignature]);
    uint32_t other = rx_unreadable.load();
    for (int i = 1; i < (int)cb::RemoteResult::BadSignature; i++) other += cmd_result[i];
    j.kv("rejected_other", (unsigned long)other);
    j.kv("opened", (unsigned long)opened);
  }
  j.end_obj();
}

#else  // indoor units have no remote opening

void cb_remote_begin() {}
void cb_remote_poll() {}
void cb_remote_on_ring(cb::UnitId) {}
bool cb_remote_phone_add(const char *, const char *) { return false; }
bool cb_remote_phone_remove(const char *) { return false; }
void cb_remote_json(cb::Json &) {}

#endif
