// cb_remote.cpp - see cb_remote.h.
#include "cb_remote.h"

#include <Arduino.h>
#include <esp_crt_bundle.h>
#include <esp_http_client.h>

#include <atomic>
#include <cstring>

#include "cb_config.h"
#include "cb_console.h"
#include "cb_link.h"
#include "cb_log.h"
#include "cb_modes.h"
#include "cb_net.h"
#include "cb_provisioning.h"
#include "cb_remote_logic.h"
#include "cb_system.h"

#if CB_IS_OUTDOOR
#include "cb_door.h"
#include "cb_door_logic.h"
#include "cb_status.h"
#endif

namespace {

constexpr const char *kServer = "https://ntfy.sh";
constexpr uint32_t kPushMaxAgeMs = 60000;  // a push that could not go out within 60 s is dropped (restart: never)

// ---- Push queue and sender (task ntfy_tx), both unit types ---------------------------------------------------
enum class Tag : uint8_t { None, Bell, Door, Warning, Ok, Test };
struct Push {
  char topic[cb::kTopicLen + 1];
  char title[48];
  char msg[160];
  char url[64];   // "Tür öffnen" button: POST url (empty = no button)
  char body[40];  // ... and its body "CBR2 <token>"
  uint8_t prio;
  Tag tag;
  bool keep;      // wait for WiFi as long as it takes (restart notice)
  uint32_t queued_ms;
};
QueueHandle_t push_q = nullptr;
std::atomic<uint32_t> pushes_ok{0}, pushes_failed{0};

const char *tag_name(Tag t) {
  switch (t) {
    case Tag::Bell: return "bell";
    case Tag::Door: return "door";
    case Tag::Warning: return "warning";
    case Tag::Ok: return "white_check_mark";
    case Tag::Test: return "test_tube";
    default: return nullptr;
  }
}

bool publish(const Push &p) {
  static char body[768];
  cb::Json j(body, sizeof body);
  j.begin_obj();
  j.kv("topic", p.topic);
  j.kv("title", p.title);
  j.kv("message", p.msg);
  if (p.prio) j.kv("priority", (int)p.prio);
  if (tag_name(p.tag)) {
    j.key("tags");
    j.begin_arr();
    j.str(tag_name(p.tag));
    j.end_arr();
  }
  if (p.url[0]) {
    // ntfy "http" action: the tap in the notification sends the request itself - no app opens (R27).
    j.key("actions");
    j.begin_arr();
    j.begin_obj();
    j.kv("action", "http");
    j.kv("label", "Tür öffnen");
    j.kv("url", p.url);
    j.kv("method", "POST");
    j.kv("body", p.body);
    j.kv("clear", true);
    j.end_obj();
    j.end_arr();
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
  static Push p;
  for (;;) {
    if (xQueueReceive(push_q, &p, portMAX_DELAY) != pdTRUE) continue;
    bool ok = false;
    auto too_old = [] { return !p.keep && millis() - p.queued_ms > kPushMaxAgeMs; };
    for (int attempt = 0; attempt < 4 && !ok && !too_old(); attempt++) {
      while (!cb_net_wifi_up() && !too_old()) vTaskDelay(pdMS_TO_TICKS(500));
      if (too_old()) break;
      ok = publish(p);
      if (!ok) vTaskDelay(pdMS_TO_TICKS(2000 * (attempt + 1)));
    }
    if (ok) pushes_ok++;
    else pushes_failed++;
    memset(p.body, 0, sizeof p.body);
  }
}

void start_sender() {
  if (push_q) return;
  push_q = xQueueCreate(10, sizeof(Push));
  // Core 0 next to WiFi, low priority: TLS handshakes take a second of CPU and must never delay door or audio.
  xTaskCreatePinnedToCore(tx_task, "ntfy_tx", 8192, nullptr, 1, nullptr, 0);
}

void queue_push(const char *topic, Tag tag, uint8_t prio, const char *title, const char *msg, bool keep = false,
                const char *url = nullptr, const char *body = nullptr) {
  if (!push_q || !topic || !topic[0]) return;
  Push p{};
  snprintf(p.topic, sizeof p.topic, "%s", topic);
  snprintf(p.title, sizeof p.title, "%s", title);
  snprintf(p.msg, sizeof p.msg, "%s", msg);
  if (url) snprintf(p.url, sizeof p.url, "%s", url);
  if (body) snprintf(p.body, sizeof p.body, "%s", body);
  p.prio = prio;
  p.tag = tag;
  p.keep = keep;
  p.queued_ms = millis();
  if (xQueueSend(push_q, &p, 0) != pdTRUE) pushes_failed++;
}

#if CB_IS_OUTDOOR
const char *flat_name(uint8_t flat) { return flat == (uint8_t)cb::UnitId::OG ? "OG" : "EG"; }

// ---- Outdoor: remote opening ------------------------------------------------------------------------------------
constexpr uint32_t kRingPushGapMs = 30000;   // a child pressing ten times sends one push, not ten
constexpr uint32_t kStreamSilentMs = 120000; // ntfy sends a keepalive every 45 s; nothing for 2 min = dead
constexpr size_t kMsgMax = 120;              // a command is 37 characters

struct Cmd {
  char topic[cb::kTopicLen + 1];
  char msg[kMsgMax + 1];
};

bool enabled = false;
QueueHandle_t cmd_q = nullptr;
cb::RemoteAuth auth;  // owned by loop()
uint32_t last_ring_push[4] = {};
bool any_ring_push[4] = {};

// The stream task reads its topic list under this lock; loop() rewrites it when phones change.
portMUX_TYPE topics_lock = portMUX_INITIALIZER_UNLOCKED;
char topics[cb::kRemotePhones * (cb::kTopicLen + 1) + 1] = "";
std::atomic<bool> resubscribe{false};

std::atomic<uint32_t> stream_connects{0}, stream_errors{0}, cmds_rx{0}, rx_unreadable{0};
std::atomic<uint32_t> last_stream_rx_ms{0};
std::atomic<bool> stream_up{false};
uint32_t cmd_result[(int)cb::RemoteResult::Expired + 1] = {};
uint32_t opened = 0, refused_lockout = 0;
char last_id[24] = "";  // id of the last ntfy message seen: a reconnect continues from there (?since=)

void topics_changed() {
  char t[sizeof topics];
  auth.cmd_topics(t, sizeof t);  // 8 x 35 characters always fit
  taskENTER_CRITICAL(&topics_lock);
  memcpy(topics, t, sizeof topics);
  taskEXIT_CRITICAL(&topics_lock);
  last_id[0] = 0;  // new set of topics: start fresh
  resubscribe = true;
}

void handle_line(const char *line) {
  char v[kMsgMax + 1];
  last_stream_rx_ms = millis();
  if (!cb::json_get_string(line, "event", v, sizeof v)) return;
  if (strcmp(v, "message") != 0) return;  // open, keepalive, poll_request
  char id[sizeof last_id];
  if (cb::json_get_string(line, "id", id, sizeof id)) memcpy(last_id, id, sizeof last_id);
  Cmd c{};
  cmds_rx++;  // every message on a command topic, readable or not
  if (!cb::json_get_string(line, "topic", c.topic, sizeof c.topic) ||
      !cb::json_get_string(line, "message", c.msg, sizeof c.msg)) {
    rx_unreadable++;  // too long or no text: cannot be ours
    return;
  }
  xQueueSend(cmd_q, &c, 0);  // full queue = flood: dropped, the loop checks at its own pace
}

void stream_once() {
  static char url[sizeof topics + 80];
  taskENTER_CRITICAL(&topics_lock);
  bool none = topics[0] == 0;
  if (!none) snprintf(url, sizeof url, "%s/%s/json%s%s", kServer, topics, last_id[0] ? "?since=" : "", last_id);
  taskEXIT_CRITICAL(&topics_lock);
  resubscribe = false;
  if (none) {
    vTaskDelay(pdMS_TO_TICKS(2000));  // no phone yet: nothing to listen to
    return;
  }
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
  while (cb_net_wifi_up() && !resubscribe && millis() - last_stream_rx_ms < kStreamSilentMs) {
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
    bool resub = resubscribe.load();
    stream_once();
    // A stream that ran for a while (or was closed for new topics) was fine: reconnect quickly.
    // Quick failures back off up to 60 s.
    backoff = (resub || millis() - t0 > 60000) ? 500 : (backoff * 2 > 60000 ? 60000 : backoff * 2);
    if (resubscribe) backoff = 500;
    vTaskDelay(pdMS_TO_TICKS(backoff));
  }
}

void push_flat(uint8_t flat, Tag tag, uint8_t prio, const char *title, const char *msg) {
  for (int s = 0; s < cb::kRemotePhones; s++)
    if (auth.used(s) && (flat == 0 || auth.phone_flat(s) == flat))
      queue_push(auth.push_topic(s), tag, prio, title, msg, false);
}

void cmd_remote(const char *) {
  uint32_t now = millis();
  cb_log("remote: %d phone(s) (EG %d, OG %d), stream %s (connects %lu, errors %lu, last data %lu s ago), tokens "
         "active %d",
         auth.phone_count(), auth.phone_count(2), auth.phone_count(3), stream_up ? "UP" : "down",
         (unsigned long)stream_connects.load(), (unsigned long)stream_errors.load(),
         (unsigned long)((now - last_stream_rx_ms) / 1000), auth.active(now));
  cb_log("remote: pushes ok %lu failed %lu; commands %lu: ok %lu, malformed %lu, scheme %lu, unknown topic %lu, token "
         "%lu, expired %lu; opened %lu, refused (lock-out) %lu",
         (unsigned long)pushes_ok.load(), (unsigned long)pushes_failed.load(), (unsigned long)cmds_rx.load(),
         (unsigned long)cmd_result[0], (unsigned long)cmd_result[1], (unsigned long)cmd_result[2],
         (unsigned long)cmd_result[3], (unsigned long)cmd_result[4], (unsigned long)cmd_result[5],
         (unsigned long)opened, (unsigned long)refused_lockout);
}

void cmd_remote_push(const char *) {
  push_flat(0, Tag::Test, 3, "ClearBell Test", "Testnachricht der Außeneinheit (Konsole remote-push).");
  cb_log("remote: test push queued for %d phone(s)", auth.phone_count());
}

#else
// ---- Indoor: outage notice (FW-B-17) ----------------------------------------------------------------------------
// The link layer calls the outdoor unit unreachable after kMissedForUnreachable missed pings (about 90 s after the
// last contact, cb_link.h); 30 s more on top -> notice about 2 min after the outage (test 12.11.: 120 s on top gave
// 4 min). The duration in the "back" notice counts from the last contact.
constexpr uint32_t kDetectLagMs = cb::kHeartbeatMs * cb::kMissedForUnreachable;
constexpr uint32_t kOutageMs = 30000;       // unreachable this long -> notice
constexpr uint32_t kStartQuietMs = 180000;  // after a start of THIS unit: give the outdoor unit time first
bool watching = false;
uint32_t down_since = 0;
bool alerted = false;
uint32_t alerts = 0;

void push_own(Tag tag, uint8_t prio, const char *title, const char *msg) {
  const CbProvisioning &p = cb_prov();
  for (int i = 0; i < CbProvisioning::kPhoneSlots; i++) {
    char push[cb::kTopicLen + 1], c[cb::kTopicLen + 1];
    if (p.phone_id[i][0] && cb::phone_topics(p.phone_key[i], push, c)) queue_push(push, tag, prio, title, msg);
  }
}

void cmd_remote(const char *) {
  cb_log("remote: indoor outage notice %s; outdoor %s%s; notices sent %lu; pushes ok %lu failed %lu",
         watching ? "ON" : "off (not paired)", cb_net_peer_reachable(cb::UnitId::Outdoor) ? "reachable" : "NOT reachable",
         alerted ? " (notice sent)" : "", (unsigned long)alerts, (unsigned long)pushes_ok.load(),
         (unsigned long)pushes_failed.load());
}

void cmd_remote_push(const char *) {
  push_own(Tag::Test, 3, "ClearBell Test", "Testnachricht der Inneneinheit (Konsole remote-push).");
  cb_log("remote: test push queued for the phones of this flat");
}
#endif

}  // namespace

void cb_remote_begin() {
  const CbProvisioning &pv = cb_prov();
  cb_console_add("remote", "pushes: remote opening (outdoor) / outage notice (indoor), counters", cmd_remote);
  cb_console_add("remote-push", "send a test push to the phones of this unit", cmd_remote_push);
  if (!pv.valid) return;
  start_sender();
#if CB_IS_OUTDOOR
  for (int i = 0; i < CbProvisioning::kPhoneSlots; i++)
    if (pv.phone_id[i][0] && !auth.set_phone(i, pv.phone_id[i], pv.phone_key[i], pv.phone_flat[i]))
      cb_log("remote: phone %d has an invalid key - ignored", i);
  cmd_q = xQueueCreate(4, sizeof(Cmd));
  topics_changed();
  xTaskCreatePinnedToCore(rx_task, "ntfy_rx", 8192, nullptr, 1, nullptr, 0);
  enabled = true;
  char m[160];
  snprintf(m, sizeof m, "%.15s ist neu gestartet. Grund: %.95s", pv.name, cb_system_reset_reason());
  for (int s = 0; s < cb::kRemotePhones; s++)  // SA 7.5: every start is announced
    if (auth.used(s)) queue_push(auth.push_topic(s), Tag::Warning, 3, "ClearBell neu gestartet", m, true);
  cb_log("remote: on, %d phone(s) (EG %d, OG %d)", auth.phone_count(), auth.phone_count(2), auth.phone_count(3));
#else
  watching = pv.has_key[(int)cb::UnitId::Outdoor];
  cb_log("remote: outage notice %s", watching ? "on" : "off (not paired)");
#endif
}

void cb_remote_poll() {
#if CB_IS_OUTDOOR
  if (!enabled) return;
  Cmd c;
  if (xQueueReceive(cmd_q, &c, 0) != pdTRUE) return;  // one per pass: a flood cannot starve the loop
  int slot = -1;
  cb::RemoteResult r = auth.check(c.topic, c.msg, millis(), &slot);
  cmd_result[(int)r]++;
  if (r != cb::RemoteResult::Ok) {
    // No push for refused commands: whoever can write to a topic could otherwise flood the phones.
    cb_log("remote: command ignored (%s)", cb::remote_result_name(r));
    return;
  }
  const char *phone = auth.phone_id(slot);
  uint8_t flat = auth.phone_flat(slot);
  if (cb_mode() == cb::Mode::Setup) {
    queue_push(auth.push_topic(slot), Tag::Warning, 3, "Tür NICHT geöffnet", "Die Außeneinheit ist in der Einrichtung.");
    return;
  }
  char who[16];
  snprintf(who, sizeof who, "app %s", phone);
  if (!cb_door_open(who, cb_door_pulse_ms())) {
    refused_lockout++;
    cb_log("remote: valid command from %s (%s) refused - lock-out time running (token stays valid)", phone,
           flat_name(flat));
    queue_push(auth.push_topic(slot), Tag::Warning, 3, "Tür NICHT geöffnet",
               "Sperrzeit läuft – in ein paar Sekunden nochmal tippen.");
    return;
  }
  auth.consume_flat(flat);  // opened: no token of this flat's recent rings works again
  opened++;
  cb_log("remote: door opened by phone %s (%s)", phone, flat_name(flat));
  cb_status_door_event(cb::DoorEvent::Remote);
  char m[96];
  snprintf(m, sizeof m, "Die Haustür wurde per App geöffnet (Handy %.8s).", phone);
  push_flat(flat, Tag::Door, 3, "Tür per App geöffnet", m);
#else
  if (!watching || millis() < kStartQuietMs) return;
  uint32_t now = millis();
  if (!cb_net_peer_reachable(cb::UnitId::Outdoor)) {
    if (!down_since) down_since = now;
    if (!alerted && now - down_since >= kOutageMs) {
      alerted = true;
      alerts++;
      cb_log("remote: outdoor unit unreachable (about %lu s since the last contact) - notice to the phones",
             (unsigned long)((kDetectLagMs + kOutageMs) / 1000));
      push_own(Tag::Warning, 4, "Außeneinheit nicht erreichbar",
               "Die Klingel an der Haustür antwortet seit etwa 2 Minuten nicht (Strom oder WLAN weg?). Klingeln "
               "kommt so lange nicht an.");
    }
  } else {
    if (alerted) {
      char m[120];
      snprintf(m, sizeof m, "Die Klingel an der Haustür ist wieder erreichbar (Ausfall etwa %lu min).",
               (unsigned long)((now - down_since + kDetectLagMs + 30000) / 60000));
      push_own(Tag::Ok, 3, "Außeneinheit wieder da", m);
      cb_log("remote: outdoor unit back after about %lu s", (unsigned long)((now - down_since + kDetectLagMs) / 1000));
    }
    alerted = false;
    down_since = 0;
  }
#endif
}

void cb_remote_on_ring(cb::UnitId flat) {
#if CB_IS_OUTDOOR
  int f = (int)flat;
  if (!enabled || (flat != cb::UnitId::EG && flat != cb::UnitId::OG) || auth.phone_count((uint8_t)f) == 0) return;
  uint32_t now = millis();
  if (any_ring_push[f] && now - last_ring_push[f] < kRingPushGapMs) return;
  any_ring_push[f] = true;
  last_ring_push[f] = now;
  int slots[cb::kPhonesPerFlat];
  char tokens[cb::kPhonesPerFlat][33];
  int n = auth.issue((uint8_t)f, now, slots, tokens);
  char title[40];
  snprintf(title, sizeof title, "Es klingelt – %s", flat_name((uint8_t)f));
  for (int i = 0; i < n; i++) {
    char url[64], body[40];
    snprintf(url, sizeof url, "%s/%s", kServer, auth.cmd_topic(slots[i]));
    snprintf(body, sizeof body, "CBR2 %s", tokens[i]);
    queue_push(auth.push_topic(slots[i]), Tag::Bell, 4, title,
               "Tür öffnen nur, wenn du weißt, wer davor steht. Gültig 5 min.", false, url, body);
    memset(tokens[i], 0, sizeof tokens[i]);
  }
#else
  (void)flat;
#endif
}

bool cb_remote_phone_add(const char *id, const char *key_hex, uint8_t flat) {
#if CB_IS_OUTDOOR
  bool ok = enabled && auth.add_phone(id, key_hex, flat) >= 0;
  if (ok) topics_changed();
  return ok;
#else
  (void)id, (void)key_hex, (void)flat;
  return false;
#endif
}

bool cb_remote_phone_remove(const char *id, uint8_t flat) {
#if CB_IS_OUTDOOR
  bool ok = auth.remove_phone(id, flat);
  if (ok) topics_changed();
  return ok;
#else
  (void)id, (void)flat;
  return false;
#endif
}

void cb_remote_welcome(const char *key_hex) {
  char push[cb::kTopicLen + 1], c[cb::kTopicLen + 1];
  if (!cb::phone_topics(key_hex, push, c)) return;
  queue_push(push, Tag::Test, 3, "ClearBell Test",
             "Dieses Handy bekommt die Nachrichten der Klingel. Bei einem Klingeln kommt hier der Knopf „Tür öffnen“.");
}

void cb_remote_json(cb::Json &j) {
  j.key("remote");
  j.begin_obj();
#if CB_IS_OUTDOOR
  j.kv("enabled", enabled);
  if (enabled) {
    uint32_t now = millis();
    j.kv("phones", auth.phone_count());
    j.kv("phones_eg", auth.phone_count(2));
    j.kv("phones_og", auth.phone_count(3));
    j.kv("stream_up", stream_up.load());
    j.kv("last_data_s", (unsigned long)((now - last_stream_rx_ms) / 1000));
    j.kv("tokens_active", auth.active(now));
    j.kv("pushes_ok", (unsigned long)pushes_ok.load());
    j.kv("pushes_failed", (unsigned long)pushes_failed.load());
    j.kv("commands", (unsigned long)cmds_rx.load());
    uint32_t rejected = rx_unreadable.load();
    for (int i = 1; i <= (int)cb::RemoteResult::Expired; i++) rejected += cmd_result[i];
    j.kv("rejected", (unsigned long)rejected);
    j.kv("opened", (unsigned long)opened);
  }
#else
  j.kv("enabled", false);  // no remote opening on indoor units
  j.key("outage");
  j.begin_obj();
  j.kv("watching", watching);
  j.kv("outdoor_reachable", cb_net_peer_reachable(cb::UnitId::Outdoor));
  j.kv("notices", (unsigned long)alerts);
  j.kv("pushes_ok", (unsigned long)pushes_ok.load());
  j.kv("pushes_failed", (unsigned long)pushes_failed.load());
  j.end_obj();
#endif
  j.end_obj();
}
