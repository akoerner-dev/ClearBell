// cb_remote.cpp - see cb_remote.h.
#include "cb_remote.h"

#include <Arduino.h>
#include <esp_crt_bundle.h>
#include <esp_http_client.h>

#include <atomic>
#include <cstring>
#include <ctime>

#include "cb_config.h"
#include "cb_console.h"
#include "cb_crypto.h"
#include "cb_link.h"
#include "cb_log.h"
#include "cb_modes.h"
#include "cb_net.h"
#include "cb_pairing_logic.h"
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

// The ONE place for the TLS settings of every HTTPS connection (S-1, R35): push sender, command stream and the
// console test "tls-probe" all call it, so the negative test checks exactly the setting used in operation.
// Certificate chain against the Mozilla bundle of the IDF; host name check stays on (esp-tls takes the URL host).
void tls_settings(esp_http_client_config_t &cfg) {
  cfg.crt_bundle_attach = esp_crt_bundle_attach;
  cfg.skip_cert_common_name_check = false;
}

// Certificates are checked against the validity dates (CONFIG_MBEDTLS_HAVE_TIME_DATE, sdkconfig.defaults). Before
// NTP has set the clock it reads 1970 and every certificate is "not yet valid" - so TLS waits for the clock.
constexpr time_t kClockValid = 1700000000;  // 2023-11, same threshold as the door log (cb_status)
bool clock_set() { return time(nullptr) >= kClockValid; }
constexpr uint32_t kPushClockWaitMs = 10000;  // a push waits at most this long for the clock, then tries anyway

// ---- Console test tls-probe (R35, SA 10.3 "TLS mit falschem Zertifikat scheitert") -------------------------------
// Opens one HTTPS connection with tls_settings() and reports whether the handshake succeeded. Runs in its own short
// task: a handshake can take seconds and must not block the console/loop task. Sends nothing to any phone.
char probe_url[128];
std::atomic<bool> probe_busy{false};

void probe_task(void *) {
  esp_http_client_config_t cfg = {};
  cfg.url = probe_url;
  cfg.method = HTTP_METHOD_GET;
  cfg.timeout_ms = 10000;
  tls_settings(cfg);
  if (!clock_set()) cb_log("tls-probe: note - clock not set (no NTP yet), every certificate counts as not yet valid");
  uint32_t t0 = millis();
  esp_http_client_handle_t c = esp_http_client_init(&cfg);
  if (!c) {
    cb_log("tls-probe: %s -> ERROR (client init failed)", probe_url);
  } else {
    esp_err_t e = esp_http_client_open(c, 0);
    if (e == ESP_OK) {
      esp_http_client_fetch_headers(c);
      cb_log("tls-probe: %s -> CONNECTED (HTTP %d, %lu ms)", probe_url, esp_http_client_get_status_code(c),
             (unsigned long)(millis() - t0));
      esp_http_client_close(c);
    } else {
      int tls_code = 0, tls_flags = 0;
      esp_err_t te = esp_http_client_get_and_clear_last_tls_error(c, &tls_code, &tls_flags);
      // tls code = mbedTLS error: -0x2700 certificate verify failed (e.g. expired, wrong host), -0x3000 from the
      // bundle check (no trusted root). The verify flags are mostly not passed through by the HTTP client (0).
      cb_log("tls-probe: %s -> REFUSED (%s; tls %s, code -0x%04x, %lu ms)", probe_url, esp_err_to_name(e),
             esp_err_to_name(te), (unsigned)(tls_code < 0 ? -tls_code : tls_code), (unsigned long)(millis() - t0));
    }
    esp_http_client_cleanup(c);
  }
  probe_busy = false;
  vTaskDelete(nullptr);
}

void cmd_tls_probe(const char *args) {
  if (strncmp(args, "https://", 8) != 0 || strlen(args) >= sizeof probe_url) {
    cb_log("tls-probe: usage tls-probe https://<host>[/path] (max %u characters)", (unsigned)(sizeof probe_url - 1));
    return;
  }
  if (!cb_net_wifi_up()) {
    cb_log("tls-probe: no WiFi");
    return;
  }
  bool expected = false;
  if (!probe_busy.compare_exchange_strong(expected, true)) {
    cb_log("tls-probe: still running, try again in a few seconds");
    return;
  }
  strcpy(probe_url, args);
  if (xTaskCreatePinnedToCore(probe_task, "tls_probe", 8192, nullptr, 1, nullptr, 0) != pdPASS) {
    probe_busy = false;
    cb_log("tls-probe: could not start the task");
  }
}

// ---- Push queue and sender (task ntfy_tx), both unit types ---------------------------------------------------
enum class Tag : uint8_t { None, Bell, Door, Warning, Ok, Test };
struct Push {
  char topic[cb::kTopicLen + 1];
  char title[64];  // "Es klingelt – " + flat name of up to 40 bytes (FW-B-21)
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
std::atomic<int> outstanding{0};  // queued or being sent (cb_remote_idle)

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
    j.kv("label", p.tag == Tag::Test ? "Ja, angekommen" : "Tür öffnen");  // FW-B-34: test push confirms
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
  tls_settings(cfg);  // certificate check against the Mozilla bundle (S-1)
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
      // Bounded: without NTP the push fails at the certificate check instead of blocking the queue (keep pushes!)
      for (uint32_t t0 = millis(); !clock_set() && millis() - t0 < kPushClockWaitMs && !too_old();)
        vTaskDelay(pdMS_TO_TICKS(250));
      if (too_old()) break;
      ok = publish(p);
      if (!ok) vTaskDelay(pdMS_TO_TICKS(2000 * (attempt + 1)));
    }
    if (ok) pushes_ok++;
    else pushes_failed++;
    memset(p.body, 0, sizeof p.body);
    outstanding--;
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
  outstanding++;
  if (xQueueSend(push_q, &p, 0) != pdTRUE) {
    outstanding--;
    pushes_failed++;
  }
}

// FW-B-34 (4): an ntfy subscription whose topic will never get a message again is told so once - otherwise it stays
// in the app with the same name as the new one, and a test seems never to arrive (Sammeltermin 24.11.2026).
constexpr const char *kFarewellTitle = "ClearBell: Abo ungültig";
const char *farewell_text(const char *why, char *out) {
  snprintf(out, 160, "Dieses Abo bekommt keine Klingel-Nachrichten mehr (%s). Bitte in der ntfy-App löschen.", why);
  return out;
}

// Farewell to the phones stored in provisioning (any task): flat 0 = all, else only that flat.
void farewell_stored(uint8_t flat, const char *why) {
  const CbProvisioning &p = cb_prov();
  char m[160];
  farewell_text(why, m);
  for (int i = 0; i < CbProvisioning::kPhoneSlots; i++) {
    char push[cb::kTopicLen + 1], c[cb::kTopicLen + 1];
    if (!p.phone_id[i][0] || (flat && p.phone_flat[i] != flat)) continue;
    if (cb::phone_topics(p.phone_key[i], push, c)) queue_push(push, Tag::Warning, 3, kFarewellTitle, m);
  }
}

#if !CB_IS_OUTDOOR
// ---- FW-B-34 (1): test push with "Ja, angekommen" - the indoor unit polls the phone's command topic ----------------
constexpr uint32_t kConfirmWaitMs = 5UL * 60 * 1000;
constexpr uint32_t kConfirmPollMs = 5000;
portMUX_TYPE confirm_lock = portMUX_INITIALIZER_UNLOCKED;
struct Confirm {
  char id[cb::kPhoneIdMax + 1];
  char cmd[cb::kTopicLen + 1];
  char nonce[cb::kConfirmNonceHex + 1];
  uint32_t started_ms;
  CbConfirm state;
} confirm = {};
std::atomic<bool> confirm_task_on{false};

bool confirm_poll_once(const char *cmd, const char *nonce) {
  static char url[96];
  snprintf(url, sizeof url, "%s/%s/json?poll=1&since=10m", kServer, cmd);
  esp_http_client_config_t cfg = {};
  cfg.url = url;
  cfg.method = HTTP_METHOD_GET;
  cfg.timeout_ms = 8000;
  cfg.buffer_size = 1024;
  tls_settings(cfg);
  esp_http_client_handle_t c = esp_http_client_init(&cfg);
  if (!c) return false;
  bool found = false;
  if (esp_http_client_open(c, 0) == ESP_OK) {
    esp_http_client_fetch_headers(c);
    if (esp_http_client_get_status_code(c) == 200) {
      static char line[1024];
      size_t len = 0;
      char buf[256];
      int n;
      while (!found && (n = esp_http_client_read(c, buf, sizeof buf)) > 0) {
        for (int i = 0; i < n && !found; i++) {
          if (buf[i] == '\n') {
            line[len] = 0;
            char msg[64];
            if (len && cb::json_get_string(line, "message", msg, sizeof msg) && cb::confirm_matches(msg, nonce))
              found = true;
            len = 0;
          } else if (len + 1 < sizeof line) {
            line[len++] = buf[i];
          } else {
            len = 0;
          }
        }
      }
    }
    esp_http_client_close(c);
  }
  esp_http_client_cleanup(c);
  return found;
}

void confirm_task(void *) {
  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(kConfirmPollMs));
    char cmd[cb::kTopicLen + 1], nonce[cb::kConfirmNonceHex + 1];
    taskENTER_CRITICAL(&confirm_lock);
    bool waiting = confirm.state == CbConfirm::Waiting;
    bool late = waiting && millis() - confirm.started_ms > kConfirmWaitMs;
    if (late) confirm.state = CbConfirm::Timeout;
    memcpy(cmd, confirm.cmd, sizeof cmd);
    memcpy(nonce, confirm.nonce, sizeof nonce);
    taskEXIT_CRITICAL(&confirm_lock);
    if (late) cb_log("remote: test push of phone '%s' NOT confirmed within 5 min", confirm.id);
    if (!waiting || late) break;
    if (!cb_net_wifi_up() || !clock_set()) continue;
    if (!confirm_poll_once(cmd, nonce)) continue;
    taskENTER_CRITICAL(&confirm_lock);
    bool still = confirm.state == CbConfirm::Waiting && !strcmp(confirm.nonce, nonce);
    if (still) confirm.state = CbConfirm::Ok;
    taskEXIT_CRITICAL(&confirm_lock);
    if (still) {
      cb_log("remote: phone '%s' confirmed the test push (\"Ja, angekommen\") - the whole chain works", confirm.id);
      break;
    }
  }
  confirm_task_on = false;
  vTaskDelete(nullptr);
}
#endif

#if CB_IS_OUTDOOR
// Log label only (EG = key at the bottom, OG = top); buyers see cb_prov_flat_name() (FW-B-21)
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
std::atomic<uint8_t> drop_flats{0};  // VK-01: bit (1 << flat) = drop that flat's phones (web task -> loop)
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
  tls_settings(cfg);
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
  bool told = false;
  for (;;) {
    if (!cb_net_wifi_up()) {
      vTaskDelay(pdMS_TO_TICKS(1000));
      continue;
    }
    if (!clock_set()) {  // the certificate check needs the date; a stream without it would only fail
      if (!told) cb_log("remote: waiting for the clock (NTP) before connecting the command stream");
      told = true;
      vTaskDelay(pdMS_TO_TICKS(500));
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
bool own_wifi_lost = false;  // FW-B-29: during this outage the unit's own WiFi was gone too
bool unpaired = false;       // FW-B-32: the outdoor unit said goodbye - no outage notice any more
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
  cb_console_add("tls-probe", "TEST: one HTTPS connection with the push TLS settings (R35), e.g. tls-probe "
                 "https://expired.badssl.com", cmd_tls_probe);
  if (!pv.valid) return;
  start_sender();
#if CB_IS_OUTDOOR
  for (int i = 0; i < CbProvisioning::kPhoneSlots; i++) {
    if (!pv.phone_id[i][0]) continue;
    if (!pv.has_key[pv.phone_flat[i] & 3]) {  // VK-01: phones of an unpaired flat are not loaded
      cb_log("remote: phone %d belongs to an unpaired flat - not loaded", i);
      continue;
    }
    if (!auth.set_phone(i, pv.phone_id[i], pv.phone_key[i], pv.phone_flat[i]))
      cb_log("remote: phone %d has an invalid key - ignored", i);
  }
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
  uint8_t drop = drop_flats.exchange(0);  // VK-01: set by the web task when a pairing went
  for (uint8_t f : {(uint8_t)cb::UnitId::EG, (uint8_t)cb::UnitId::OG}) {
    if (!(drop & (1u << f))) continue;
    char m[160];
    push_flat(f, Tag::Warning, 3, kFarewellTitle, farewell_text("die Kopplung der Wohnung wurde entfernt", m));
    int n = auth.remove_flat(f);
    if (n) topics_changed();
    cb_log("remote: pairing of %s gone - %d phone(s) and their tokens dropped", flat_name(f), n);
  }
  Cmd c;
  if (xQueueReceive(cmd_q, &c, 0) != pdTRUE) return;  // one per pass: a flood cannot starve the loop
  if (cb::is_confirm_message(c.msg)) return;  // FW-B-34: "Ja, angekommen" of a test push - for the indoor unit
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
  if (!cb_prov().has_key[flat]) {  // VK-01, second line: a flat without pairing opens nothing
    cb_log("remote: command from %s refused - %s is not paired", phone, flat_name(flat));
    return;
  }
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
  if (!watching || unpaired || millis() < kStartQuietMs) return;
  uint32_t now = millis();
  if (!cb_net_peer_reachable(cb::UnitId::Outdoor)) {
    if (!down_since) down_since = now;
    if (!cb_net_wifi_up()) own_wifi_lost = true;  // FW-B-29: this unit itself has no WiFi (router restart?)
    if (!alerted && !own_wifi_lost && now - down_since >= kOutageMs) {
      alerted = true;
      alerts++;
      cb_log("remote: outdoor unit unreachable (about %lu s since the last contact) - notice to the phones",
             (unsigned long)((kDetectLagMs + kOutageMs) / 1000));
      push_own(Tag::Warning, 4, "Außeneinheit nicht erreichbar",
               "Die Klingel an der Haustür antwortet seit etwa 2 Minuten nicht (Strom oder WLAN weg?). Klingeln "
               "kommt so lange nicht an.");
    }
  } else {
    unsigned long min = (unsigned long)((now - down_since + kDetectLagMs + 30000) / 60000);
    if (alerted) {
      char m[120];
      snprintf(m, sizeof m, "Die Klingel an der Haustür ist wieder erreichbar (Ausfall etwa %lu min).", min);
      push_own(Tag::Ok, 3, "Außeneinheit wieder da", m);
      cb_log("remote: outdoor unit back after about %lu s", (unsigned long)((now - down_since + kDetectLagMs) / 1000));
    } else if (own_wifi_lost && down_since && now - down_since >= kOutageMs) {
      // FW-B-29: while this unit had no WiFi no notice could go out (it was dropped before). Now one notice that says
      // what happened, instead of a lone "wieder erreichbar" without the "nicht erreichbar" before it.
      char m[160];
      snprintf(m, sizeof m, "Das WLAN war etwa %lu min weg (Router neu gestartet?). In der Zeit kam Klingeln "
               "nicht an. Jetzt ist wieder alles erreichbar.", min);
      push_own(Tag::Ok, 3, "Klingel wieder erreichbar", m);
      alerts++;
      cb_log("remote: WiFi of this unit was gone, outdoor unit back after about %lu s - one notice afterwards",
             (unsigned long)((now - down_since + kDetectLagMs) / 1000));
    }
    alerted = false;
    own_wifi_lost = false;
    down_since = 0;
  }
#endif
}

void cb_remote_on_ring(cb::UnitId flat) {
#if CB_IS_OUTDOOR
  int f = (int)flat;
  if (!enabled || (flat != cb::UnitId::EG && flat != cb::UnitId::OG) || auth.phone_count((uint8_t)f) == 0) return;
  if (!cb_prov().has_key[f]) return;  // VK-01: no tokens for a flat without pairing
  uint32_t now = millis();
  if (any_ring_push[f] && now - last_ring_push[f] < kRingPushGapMs) return;
  any_ring_push[f] = true;
  last_ring_push[f] = now;
  int slots[cb::kPhonesPerFlat];
  char tokens[cb::kPhonesPerFlat][33];
  int n = auth.issue((uint8_t)f, now, slots, tokens);
  char title[64];  // FW-B-21: the flat's own name (<= 40 bytes) - fits without cutting a UTF-8 character
  snprintf(title, sizeof title, "Es klingelt – %s", cb_prov_flat_name(flat));
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

void cb_remote_drop_flat(uint8_t flat) {
#if CB_IS_OUTDOOR
  if (flat == (uint8_t)cb::UnitId::EG || flat == (uint8_t)cb::UnitId::OG) drop_flats.fetch_or((uint8_t)(1u << flat));
#else
  (void)flat;
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

bool cb_remote_test(const char *id, const char *key_hex) {
  char push[cb::kTopicLen + 1], c[cb::kTopicLen + 1];
  if (!cb::phone_topics(key_hex, push, c)) return false;
#if !CB_IS_OUTDOOR
  // FW-B-34 (1): the button sends "CBOK <nonce>" to the phone's command topic; a new test replaces a waiting one
  uint8_t r[cb::kConfirmNonceHex / 2];
  cb::random_bytes(r, sizeof r);
  char nonce[cb::kConfirmNonceHex + 1], url[64], body[24];
  cb::to_hex_lower(r, sizeof r, nonce);
  snprintf(url, sizeof url, "%s/%s", kServer, c);
  snprintf(body, sizeof body, "%s %s", cb::kConfirmScheme, nonce);
  taskENTER_CRITICAL(&confirm_lock);
  snprintf(confirm.id, sizeof confirm.id, "%s", id);
  memcpy(confirm.cmd, c, sizeof confirm.cmd);
  memcpy(confirm.nonce, nonce, sizeof confirm.nonce);
  confirm.started_ms = millis();
  confirm.state = CbConfirm::Waiting;
  taskEXIT_CRITICAL(&confirm_lock);
  queue_push(push, Tag::Test, 3, "ClearBell Test",
             "Dieses Handy bekommt die Nachrichten der Klingel. Bitte „Ja, angekommen“ tippen – dann zeigt die "
             "Statusseite, dass alles funktioniert.", false, url, body);
  bool expected = false;
  if (confirm_task_on.compare_exchange_strong(expected, true) &&
      xTaskCreatePinnedToCore(confirm_task, "ntfy_ok", 8192, nullptr, 1, nullptr, 0) != pdPASS)
    confirm_task_on = false;
  cb_log("remote: test push to phone '%s' - waiting up to 5 min for \"Ja, angekommen\"", id);
#else
  (void)id;
  queue_push(push, Tag::Test, 3, "ClearBell Test", "Testnachricht der Außeneinheit.");
#endif
  return true;
}

CbConfirm cb_remote_confirm_state(char id[cb::kPhoneIdMax + 1], uint32_t *left_s) {
#if !CB_IS_OUTDOOR
  taskENTER_CRITICAL(&confirm_lock);
  CbConfirm st = confirm.state;
  memcpy(id, confirm.id, cb::kPhoneIdMax + 1);
  uint32_t el = millis() - confirm.started_ms;
  if (st == CbConfirm::Waiting && el > kConfirmWaitMs + 2 * kConfirmPollMs) st = CbConfirm::Timeout;  // poller gone
  taskEXIT_CRITICAL(&confirm_lock);
  if (left_s) *left_s = st == CbConfirm::Waiting && el < kConfirmWaitMs ? (kConfirmWaitMs - el + 999) / 1000 : 0;
  return st;
#else
  id[0] = 0;
  if (left_s) *left_s = 0;
  return CbConfirm::None;
#endif
}

void cb_remote_confirm_forget(const char *id) {
#if !CB_IS_OUTDOOR
  taskENTER_CRITICAL(&confirm_lock);
  if (!strcmp(confirm.id, id)) confirm.state = CbConfirm::None;
  taskEXIT_CRITICAL(&confirm_lock);
#else
  (void)id;
#endif
}

void cb_remote_farewell(const char *key_hex, const char *why) {
  char push[cb::kTopicLen + 1], c[cb::kTopicLen + 1], m[160];
  if (cb::phone_topics(key_hex, push, c)) queue_push(push, Tag::Warning, 3, kFarewellTitle, farewell_text(why, m));
}

void cb_remote_farewell_all(const char *why) {
  start_sender();  // a unit without phones loaded may not have started it
  farewell_stored(0, why);
}

bool cb_remote_idle() {
  return outstanding.load() <= 0;
}

void cb_remote_unpaired() {
#if !CB_IS_OUTDOOR
  unpaired = true;  // FW-B-32: no outage notice to phones that are gone
#endif
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
