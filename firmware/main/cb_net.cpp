// cb_net.cpp - see cb_net.h.
#include "cb_net.h"

#include <Arduino.h>
#include <AsyncUDP.h>
#include <ESPmDNS.h>
#include <WiFi.h>
#include <esp_wifi.h>

#include <cstring>

#include "cb_config.h"
#include "cb_console.h"
#include "cb_crypto.h"
#include "cb_link.h"
#include "cb_log.h"
#include "cb_provisioning.h"
#include "cb_ratelimit.h"
#include "cb_remote.h"
#include "cb_update.h"

using cb::UnitId;

namespace {

constexpr uint16_t kPort = 4210;            // protocol v2 between the units (V0.1 used the same port)
constexpr uint32_t kBackoffMinMs = 1000;
constexpr uint32_t kBackoffMaxMs = 60000;
constexpr uint32_t kAttemptTimeoutMs = 30000;  // an attempt that never reports back is given up after this
constexpr int kRxQueueLen = 16;               // datagrams waiting for loop(); more = dropped (flood)

// ---- Datagram hand-over UDP task -> loop ---------------------------------------------------------------
struct Rx {
  uint32_t ip;  // source address (lwIP byte order)
  uint8_t len;
  uint8_t data[cb::kMaxFrame];
};
QueueHandle_t rx_queue = nullptr;
uint32_t rx_queue_full = 0, rx_oversize = 0;

// ---- WiFi events -> loop ------------------------------------------------------------------------------
enum class WifiEv : uint8_t { GotIp, Disconnected };
struct WifiMsg {
  WifiEv ev;
  uint8_t reason;
};
QueueHandle_t wifi_queue = nullptr;

enum class WifiState : uint8_t { Connecting, Up, Waiting };
WifiState wifi_state = WifiState::Waiting;
uint32_t attempt_started = 0, next_attempt = 0, backoff = kBackoffMinMs;
uint32_t disconnects = 0;
uint8_t last_reason = 0;
bool mdns_started = false;
bool sntp_started = false;
int sleep_override = -1;  // console 'sleep': -1 = automatic, 0 = off, 1 = on
bool sleep_now = false;

// ---- Peers --------------------------------------------------------------------------------------------
enum class AddrSource : uint8_t { None, Fixed, Learned, Mdns };
struct PeerAddr {
  uint32_t ip = 0;
  AddrSource src = AddrSource::None;
};
PeerAddr addr[4];
portMUX_TYPE addr_lock = portMUX_INITIALIZER_UNLOCKED;  // the mDNS task writes, loop reads
char hostname[24];
uint32_t mdns_queries = 0, mdns_hits = 0;  // written by the resolver task, only displayed

// Role names in the mDNS service record "_clearbell._udp" (TXT "role"), see resolve_peer().
const char *role_txt(UnitId u) {
  switch (u) {
    case UnitId::Outdoor: return "aussen";
    case UnitId::EG: return "eg";
    case UnitId::OG: return "og";
    default: return "";
  }
}

// Address of a peer: by its unit number if pairing stored one (clearbell-<number>.local), else the ClearBell in the
// network that announces the wanted role. A wrong choice (two installations in one network) is harmless: its
// frames fail the HMAC check, and the search simply continues.
uint32_t resolve_peer(UnitId u) {
  const char *serial = cb_prov().peer_serial[(int)u];
  if (serial[0]) {
    char host[24];
    snprintf(host, sizeof host, "clearbell-%s", serial);
    return (uint32_t)MDNS.queryHost(host, 2000);
  }
  int n = MDNS.queryService("clearbell", "udp");
  for (int k = 0; k < n; k++)
    if (MDNS.txt(k, "role") == role_txt(u)) return (uint32_t)MDNS.address(k);
  return 0;
}

const char *src_name(AddrSource s) {
  switch (s) {
    case AddrSource::Fixed: return "fixed";
    case AddrSource::Learned: return "learned";
    case AddrSource::Mdns: return "mdns";
    default: return "none";
  }
}

AsyncUDP udp;
cb::Link engine;  // the protocol engine; owned by loop() alone

// ---- Measurement series "ring eg 50" (bring-up / acceptance: ring -> ack time, Softwarearchitektur 1.3) --
constexpr int kSeriesMax = 300;
uint32_t ring_started[4] = {};
struct Series {
  UnitId flat = UnitId::None;
  int remaining = 0, sent = 0, ok = 0, failed = 0;
  uint32_t next_at = 0;
  uint16_t ms[kSeriesMax];
} series;

int cmp_u16(const void *a, const void *b) {
  return (int)*(const uint16_t *)a - (int)*(const uint16_t *)b;
}

void series_report() {
  int n = series.ok;
  qsort(series.ms, n, sizeof(series.ms[0]), cmp_u16);
  int over = 0;
  for (int i = 0; i < n; i++) over += series.ms[i] > cb::kAckTimeoutMs;
  cb_log("series %s: %d sent, %d acknowledged, %d failed; ack ms min %u median %u 95%% %u max %u; over %lu ms: %d",
         cb::unit_name(series.flat), series.sent, series.ok, series.failed, n ? series.ms[0] : 0,
         n ? series.ms[n / 2] : 0, n ? series.ms[(n - 1) * 95 / 100] : 0, n ? series.ms[n - 1] : 0,
         (unsigned long)cb::kAckTimeoutMs, over);
  series.flat = UnitId::None;
}

struct Handler : cb::LinkHandler {
  void link_send(UnitId peer, const uint8_t *frame, size_t len) override {
    uint32_t ip;
    taskENTER_CRITICAL(&addr_lock);
    ip = addr[(int)peer].ip;
    taskEXIT_CRITICAL(&addr_lock);
    if (!ip) return;  // address not known yet: the retransmission / next heartbeat tries again
    udp.writeTo(frame, len, IPAddress(ip), kPort);
  }
  void link_ring(UnitId) override { cb_on_ring_received(); }
  void link_ring_result(UnitId to, bool ok) override {
    uint32_t dt = millis() - ring_started[(int)to];
    if (series.flat == to) {
      if (ok && series.ok < kSeriesMax) series.ms[series.ok++] = (uint16_t)(dt > 65535 ? 65535 : dt);
      if (!ok) series.failed++;
      if (series.remaining == 0 && series.ok + series.failed >= series.sent) series_report();
      return;  // a series does not flood the log or the LED with single results
    }
    cb_log("net: ring %s %s after %lu ms", cb::unit_name(to), ok ? "acknowledged" : "failed", (unsigned long)dt);
    cb_on_ring_result(to, ok);
  }
  void link_peer_state(UnitId peer, bool reachable) override {
    cb_log("net: %s %s", cb::unit_name(peer), reachable ? "reachable" : "UNREACHABLE");
  }
  cb::DoorResult link_door_decide(UnitId from) override { return cb_on_door_decide(from); }
  void link_door_execute(UnitId from) override { cb_on_door_open(from); }
  void link_door_result(cb::DoorResult r, bool answered) override {
    cb_on_door_result(r, answered, millis() - door_started);
  }
  void link_event(UnitId from, cb::EventCode code, const uint8_t *data, size_t len) override {
    cb_log("net: event %u from %s", (unsigned)code, cb::unit_name(from));
    cb_on_event(from, code, data, len);
  }
  void link_event_result(UnitId to, cb::EventCode code, bool ok) override {
    cb_log("net: event %u to %s %s", (unsigned)code, cb::unit_name(to), ok ? "acknowledged" : "FAILED");
    cb_on_event_result(to, code, ok);
  }
  uint32_t door_started = 0;
} handler;

// ---- Callbacks from other tasks: copy and queue, nothing else ------------------------------------------

cb::RateLimiter limiter;  // used only in the UDP task (on_packet), so it needs no lock
uint32_t rx_rate_limited = 0;

void on_packet(AsyncUDPPacket &p) {
  if (p.length() > cb::kMaxFrame || p.length() < cb::kMinFrame) {
    rx_oversize++;
    return;  // cannot be a ClearBell frame - not even worth a queue slot
  }
  const uint8_t *d = p.data();
  if (d[0] != cb::kMagic0 || d[1] != cb::kMagic1) {
    rx_oversize++;  // foreign protocol: dropped before rate limit and queue, like a wrong length
    return;
  }
  // Rate limit per source BEFORE the queue and the HMAC (Softwarearchitektur 4.2 order).
  if (!limiter.allow((uint32_t)p.remoteIP(), millis())) {
    rx_rate_limited++;
    return;
  }
  Rx rx;
  rx.ip = (uint32_t)p.remoteIP();
  rx.len = (uint8_t)p.length();
  memcpy(rx.data, p.data(), rx.len);
  if (xQueueSend(rx_queue, &rx, 0) != pdTRUE) rx_queue_full++;
}

void on_wifi(arduino_event_id_t e, arduino_event_info_t info) {
  WifiMsg m{};
  if (e == ARDUINO_EVENT_WIFI_STA_GOT_IP) m.ev = WifiEv::GotIp;
  else if (e == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) {
    m.ev = WifiEv::Disconnected;
    m.reason = info.wifi_sta_disconnected.reason;
  } else return;
  xQueueSend(wifi_queue, &m, 0);
}

// ---- mDNS resolver: slow and blocking, therefore its own low-priority task ----------------------------

void resolver_task(void *) {
  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(5000));
    if (!mdns_started) continue;
    for (int i = 1; i < 4; i++) {
      UnitId u = (UnitId)i;
      if (!engine.has_key(u)) continue;
      AddrSource s;
      taskENTER_CRITICAL(&addr_lock);
      s = addr[i].src;
      taskEXIT_CRITICAL(&addr_lock);
      if (s == AddrSource::Fixed || s == AddrSource::Learned) continue;
      IPAddress ip(resolve_peer(u));
      mdns_queries++;
      if ((uint32_t)ip != 0) {
        mdns_hits++;
        taskENTER_CRITICAL(&addr_lock);
        if (addr[i].src != AddrSource::Fixed && addr[i].src != AddrSource::Learned) {
          addr[i].ip = (uint32_t)ip;
          addr[i].src = AddrSource::Mdns;
        }
        taskEXIT_CRITICAL(&addr_lock);
      }
    }
  }
}

// ---- WiFi state machine ----------------------------------------------------------------------------------

void start_attempt(uint32_t now) {
  if (cb_update_skip_wifi()) {  // update test fault "nowifi": the stack runs, but never connects
    wifi_state = WifiState::Waiting;
    next_attempt = now + kBackoffMaxMs;
    return;
  }
  wifi_state = WifiState::Connecting;
  attempt_started = now;
  WiFi.begin(cb_prov().wifi_ssid, cb_prov().wifi_pass);
}

void apply_sleep() {
  // Automatic: off while a transaction of OURS waits for its answer (outdoor: ring, indoor: door command),
  // on otherwise (M-23). The waiting side then hears the answer without DTIM delay.
  bool want = sleep_override >= 0 ? sleep_override == 1 : !engine.ring_in_flight();
  if (want != sleep_now) {
    sleep_now = want;
    WiFi.setSleep(want ? WIFI_PS_MIN_MODEM : WIFI_PS_NONE);
  }
}

void handle_wifi(uint32_t now) {
  WifiMsg m;
  while (xQueueReceive(wifi_queue, &m, 0) == pdTRUE) {
    if (m.ev == WifiEv::GotIp) {
      wifi_state = WifiState::Up;
      backoff = kBackoffMinMs;
      if (!mdns_started) {
        mdns_started = MDNS.begin(hostname);
        MDNS.addService("http", "tcp", 80);  // the status page (cb_status)
        MDNS.addService("clearbell", "udp", kPort);  // found by the peers (and by pairing)
        MDNS.addServiceTxt("clearbell", "udp", "role", role_txt(cb_prov().role));
        MDNS.addServiceTxt("clearbell", "udp", "id", cb_prov().serial);
        // Indoor: shown on the outdoor unit's pairing list ("already paired?")
        MDNS.addServiceTxt("clearbell", "udp", "paired", cb_prov().has_key[(int)UnitId::Outdoor] ? "1" : "0");
        cb_net_mdns_update_name();
      }
      if (!sntp_started) {
        // Clock for the door log (SA 6.4). Without internet it simply stays unset; the log then writes
        // "seconds since boot". Time zone Germany with summer time.
        configTzTime("CET-1CEST,M3.5.0,M10.5.0/3", "pool.ntp.org", "time.cloudflare.com");
        sntp_started = true;
      }
      engine.set_network(true, now);
      cb_log("net: WiFi up, IP %s, RSSI %d dBm, channel %d", WiFi.localIP().toString().c_str(), (int)WiFi.RSSI(),
             (int)WiFi.channel());
    } else {
      if (wifi_state == WifiState::Up) disconnects++;
      last_reason = m.reason;
      engine.set_network(false, now);
      // The attempt (or the connection) has ended: wait, then try again with a longer pause each time.
      wifi_state = WifiState::Waiting;
      next_attempt = now + backoff;
      cb_log("net: WiFi down (reason %u), next attempt in %lu s", m.reason, (unsigned long)(backoff / 1000));
      backoff = backoff * 2 > kBackoffMaxMs ? kBackoffMaxMs : backoff * 2;
    }
  }
  if (wifi_state == WifiState::Waiting && (int32_t)(now - next_attempt) >= 0) start_attempt(now);
  if (wifi_state == WifiState::Connecting && now - attempt_started > kAttemptTimeoutMs) {
    // No event for 30 s: the driver lost track of the attempt. Only now is it safe to restart it.
    cb_log("net: attempt silent for %lu s, restarting it", (unsigned long)(kAttemptTimeoutMs / 1000));
    WiFi.disconnect();
    wifi_state = WifiState::Waiting;
    next_attempt = now + backoff;
  }
}

// ---- Console -----------------------------------------------------------------------------------------------

void cmd_net(const char *) {
  const char *st = wifi_state == WifiState::Up ? "up" : wifi_state == WifiState::Connecting ? "connecting" : "waiting";
  cb_log("net: WiFi %s, IP %s, RSSI %d dBm, channel %d, disconnects %lu, last reason %u, modem sleep %s%s", st,
         WiFi.localIP().toString().c_str(), (int)WiFi.RSSI(), (int)WiFi.channel(), (unsigned long)disconnects,
         last_reason, sleep_now ? "on" : "off", sleep_override >= 0 ? " (forced)" : "");
  for (int i = 1; i < 4; i++) {
    UnitId u = (UnitId)i;
    if (!engine.has_key(u)) continue;
    cb_log("net: peer %-6s %-15s (%s) session %s, %s", cb::unit_name(u), IPAddress(addr[i].ip).toString().c_str(),
           src_name(addr[i].src), engine.session_known(u) ? "confirmed" : "unknown",
           engine.reachable(u) ? "reachable" : "unreachable");
  }
  const cb::LinkStats &s = engine.stats();
  cb_log("net: rx ok %lu, dropped tag %lu addr %lu ver %lu len %lu magic %lu, unknown session %lu, dup %lu, "
         "old %lu, unexpected %lu, queue full %lu",
         (unsigned long)s.rx_ok, (unsigned long)s.rx_dropped[(int)cb::RxError::BadTag],
         (unsigned long)s.rx_dropped[(int)cb::RxError::BadAddress],
         (unsigned long)s.rx_dropped[(int)cb::RxError::BadVersion],
         (unsigned long)(s.rx_dropped[(int)cb::RxError::BadLength] + rx_oversize),
         (unsigned long)s.rx_dropped[(int)cb::RxError::BadMagic], (unsigned long)s.rx_unknown_session,
         (unsigned long)s.rx_duplicate, (unsigned long)s.rx_too_old, (unsigned long)s.rx_unexpected,
         (unsigned long)rx_queue_full);
  cb_log("net: rate-limited %lu (per source, before HMAC), mDNS %s, queries %lu, answered %lu",
         (unsigned long)rx_rate_limited, mdns_started ? "running" : "not started", (unsigned long)mdns_queries,
         (unsigned long)mdns_hits);
  cb_log("net: door challenges %lu, opened %lu, refused %lu, bad nonce %lu, failed %lu",
         (unsigned long)s.door_challenges, (unsigned long)s.door_opened, (unsigned long)s.door_refused,
         (unsigned long)s.door_bad_nonce, (unsigned long)s.door_failed);
  cb_log("net: tx %lu, retries %lu, rings ok %lu failed %lu no-slot %lu, sessions accepted %lu",
         (unsigned long)s.tx_frames, (unsigned long)s.tx_retries, (unsigned long)s.ring_ok,
         (unsigned long)s.ring_failed, (unsigned long)s.ring_no_slot, (unsigned long)s.sessions_accepted);
}

void cmd_ring(const char *args) {
#if CB_IS_OUTDOOR
  UnitId flat = strncmp(args, "eg", 2) == 0 ? UnitId::EG : strncmp(args, "og", 2) == 0 ? UnitId::OG : UnitId::None;
  if (flat == UnitId::None) {
    cb_log("usage: ring eg|og [count]  - with count: a series, one ring every 2 s, then statistics");
    return;
  }
  int count = atoi(args + 2);
  if (count > 1) {
    series = Series();
    series.flat = flat;
    series.remaining = count > kSeriesMax ? kSeriesMax : count;
    series.next_at = millis();
    cb_log("series %s: %d rings, one every 2 s", cb::unit_name(flat), series.remaining);
    return;
  }
  cb_log("ring %.2s: %s", args, cb_net_ring(flat) ? "sent, waiting for ack" : "FAILED at once (no WiFi / no slot)");
#else
  (void)args;
  cb_log("ring: only the outdoor unit rings");
#endif
}

// Indoor: door command from the console - stands in for the touch key until port step 6. The console needs
// the USB cable at the unit, i.e. physical access, like the key itself.
void cmd_door(const char *) {
#if CB_IS_OUTDOOR
  cb_log("door: only an indoor unit sends door commands");
#else
  cb_log("door: %s", cb_net_door_request() ? "request sent" : "FAILED at once (no WiFi / already running)");
#endif
}

// Robustness test (Softwarearchitektur 11.2 "WLAN im Betrieb abschalten und zurückbringen"): drops the
// connection like a router restart would. Expected: WiFi down, reconnect after the back-off, sessions
// re-confirmed by heartbeat, no reboot.
void cmd_wifi_drop(const char *) {
  cb_log("wifi-drop: disconnecting now - expect reconnect without reboot");
  esp_wifi_disconnect();
}

void cmd_sleep(const char *args) {
  sleep_override = *args == '0' ? 0 : *args == '1' ? 1 : -1;
  sleep_now = !sleep_now;  // force apply_sleep() to re-apply
  apply_sleep();
  cb_log("modem sleep %s", sleep_override < 0 ? "automatic" : sleep_override ? "forced on" : "forced off");
}

}  // namespace

// ---- Public -----------------------------------------------------------------------------------------------------

void cb_net_begin() {
  const CbProvisioning &p = cb_prov();
  rx_queue = xQueueCreate(kRxQueueLen, sizeof(Rx));
  wifi_queue = xQueueCreate(8, sizeof(WifiMsg));

  snprintf(hostname, sizeof(hostname), "clearbell-%s", p.serial);  // unique per unit: two installations coexist
  WiFi.onEvent(on_wifi);
  WiFi.persistent(false);       // credentials come from provisioning, not from the WiFi driver's own NVS
  WiFi.setAutoReconnect(false); // reconnecting is done by handle_wifi() with back-off
  WiFi.mode(WIFI_STA);
  esp_wifi_set_country_code("DE", false);
  WiFi.setScanMethod(WIFI_ALL_CHANNEL_SCAN);
  WiFi.setSortMethod(WIFI_CONNECT_AP_BY_SIGNAL);
  WiFi.setHostname(hostname);

  // The session number is drawn now that the radio runs: the hardware RNG is truly random from here on.
  uint32_t session = 0;
  while (session == 0) cb::random_bytes((uint8_t *)&session, sizeof(session));
  engine.begin(p.role, session, &handler, millis());
  for (int i = 1; i < 4; i++) {
    if (p.has_key[i]) engine.set_key((UnitId)i, p.key[i]);
    if (p.peer_ip[i]) addr[i] = {p.peer_ip[i], AddrSource::Fixed};
  }

  if (!udp.listen(kPort)) cb_log("net: ERROR - UDP port %u not opened", kPort);
  udp.onPacket(on_packet);
  xTaskCreatePinnedToCore(resolver_task, "mdns_res", 4096, nullptr, 1, nullptr, 0);

  cb_console_add("net", "WiFi, peers and protocol counters", cmd_net);
  cb_console_add("ring", "ring eg|og - outdoor: ring a flat now", cmd_ring);
  cb_console_add("sleep", "sleep 0|1|auto - modem sleep for measurements", cmd_sleep);
  cb_console_add("wifi-drop", "drop the WiFi connection (reconnect test)", cmd_wifi_drop);
  cb_console_add("door", "indoor: send a door command (stands in for the touch key)", cmd_door);

  sleep_now = false;
  apply_sleep();  // modem sleep on (idle)
  start_attempt(millis());
  cb_log("net: connecting to '%s' as %s.local, UDP port %u", p.wifi_ssid, hostname, kPort);
}

void cb_net_poll() {
  uint32_t now = millis();
  handle_wifi(now);
  Rx rx;
  // Bounded per pass, so a flood cannot starve the rest of loop().
  for (int n = 0; n < 4 && xQueueReceive(rx_queue, &rx, 0) == pdTRUE; n++) {
    UnitId sender, receiver;
    if (engine.receive(rx.data, rx.len, now) == cb::RxError::Ok &&
        cb::precheck_frame(rx.data, rx.len, &sender, &receiver) == cb::RxError::Ok) {
      // Authenticated frame: remember where this peer is (unless an address is fixed by provisioning).
      taskENTER_CRITICAL(&addr_lock);
      if (addr[(int)sender].src != AddrSource::Fixed) addr[(int)sender] = {rx.ip, AddrSource::Learned};
      taskEXIT_CRITICAL(&addr_lock);
    }
  }
  // A peer address that was unknown until now (typically: mDNS answered after a reboot) -> ping at once.
  static uint32_t known_ip[4] = {};
  for (int i = 1; i < 4; i++) {
    uint32_t ip;
    taskENTER_CRITICAL(&addr_lock);
    ip = addr[i].ip;
    taskEXIT_CRITICAL(&addr_lock);
    if (ip && !known_ip[i]) engine.ping_soon((UnitId)i, now);
    known_ip[i] = ip;
  }
  if (series.remaining > 0 && (int32_t)(now - series.next_at) >= 0) {
    series.remaining--;
    series.sent++;
    series.next_at = now + 2000;
    if (!cb_net_ring(series.flat)) series.failed++;
    if (series.remaining == 0 && series.ok + series.failed >= series.sent) series_report();
  }
  engine.tick(now);
  apply_sleep();
}

void cb_net_json(cb::Json &j) {
  const char *st = wifi_state == WifiState::Up ? "up" : wifi_state == WifiState::Connecting ? "connecting" : "waiting";
  j.key("radio");
  j.begin_obj();
  j.kv("wifi", st);
  bool up = wifi_state == WifiState::Up;
  j.kv("ip", up ? WiFi.localIP().toString().c_str() : "");
  j.kv("rssi", up ? (int)WiFi.RSSI() : 0);
  j.kv("channel", up ? (int)WiFi.channel() : 0);
  j.kv("bssid", up ? WiFi.BSSIDstr().c_str() : "");
  j.kv("disconnects", (unsigned long)disconnects);
  j.kv("last_reason", (unsigned)last_reason);
  j.kv("modem_sleep", sleep_now);
  j.kv("hostname", hostname);
  j.end_obj();

  const cb::LinkStats &s = engine.stats();
  j.key("protocol");
  j.begin_obj();
  j.key("peers");
  j.begin_arr();
  for (int i = 1; i < 4; i++) {
    UnitId u = (UnitId)i;
    if (!engine.has_key(u)) continue;
    uint32_t ip;
    AddrSource src;
    taskENTER_CRITICAL(&addr_lock);
    ip = addr[i].ip;
    src = addr[i].src;
    taskEXIT_CRITICAL(&addr_lock);
    j.begin_obj();
    j.kv("unit", cb::unit_name(u));
    j.kv("ip", IPAddress(ip).toString().c_str());
    j.kv("address_from", src_name(src));
    j.kv("session", engine.session_known(u));
    j.kv("reachable", engine.reachable(u));
    j.end_obj();
  }
  j.end_arr();
  j.kv("tx", (unsigned long)s.tx_frames);
  j.kv("retries", (unsigned long)s.tx_retries);
  j.kv("rings_ok", (unsigned long)s.ring_ok);
  j.kv("rings_failed", (unsigned long)s.ring_failed);
  j.kv("rings_no_slot", (unsigned long)s.ring_no_slot);
  j.kv("rx_ok", (unsigned long)s.rx_ok);
  j.kv("rx_bad_tag", (unsigned long)s.rx_dropped[(int)cb::RxError::BadTag]);
  j.kv("rx_bad_other", (unsigned long)(s.rx_dropped[(int)cb::RxError::BadAddress] +
                                       s.rx_dropped[(int)cb::RxError::BadVersion] +
                                       s.rx_dropped[(int)cb::RxError::BadLength] +
                                       s.rx_dropped[(int)cb::RxError::BadMagic] + rx_oversize));
  j.kv("rx_duplicate", (unsigned long)s.rx_duplicate);
  j.kv("rx_too_old", (unsigned long)s.rx_too_old);
  j.kv("rx_unknown_session", (unsigned long)s.rx_unknown_session);
  j.kv("rx_rate_limited", (unsigned long)rx_rate_limited);
  j.kv("rx_queue_full", (unsigned long)rx_queue_full);
  j.kv("sessions_accepted", (unsigned long)s.sessions_accepted);
  j.kv("door_challenges", (unsigned long)s.door_challenges);
  j.kv("door_opened", (unsigned long)s.door_opened);
  j.kv("door_refused", (unsigned long)s.door_refused);
  j.kv("door_bad_nonce", (unsigned long)s.door_bad_nonce);
  j.kv("door_failed", (unsigned long)s.door_failed);
  j.end_obj();
}

bool cb_net_wifi_up() {
  return wifi_state == WifiState::Up;
}

bool cb_net_peer_reachable(UnitId peer) {
  return engine.reachable(peer);
}

bool cb_net_door_request() {
  handler.door_started = millis();
  bool ok = engine.send_door_request(millis());
  apply_sleep();
  if (!ok) cb_on_door_result(cb::DoorResult::NotAllowed, false, 0);  // immediate failure: error tone at once
  return ok;
}

bool cb_net_ring(UnitId flat) {
  ring_started[(int)flat] = millis();
  bool ok = engine.send_ring(flat, millis());
  apply_sleep();  // ring in flight: modem sleep off at once (M-23)
  // Immediate failure (no WiFi, all slots busy): the visitor hears it at once (SA 5.3 "ohne WLAN sofort").
  if (!ok && series.flat != flat) cb_on_ring_result(flat, false);
  // Push to the phones of that flat (rate-limited inside) - also when the flat did not answer: then the push is the
  // only way the visitor reaches anyone. Never for a measurement series.
  if (series.flat != flat) cb_remote_on_ring(flat);
  return ok;
}

// Weak defaults so the module links on its own; main.cpp provides the real reactions.
__attribute__((weak)) void cb_on_ring_received() {
  cb_log("RING received");
}
__attribute__((weak)) void cb_on_ring_result(UnitId flat, bool ok) {
  cb_log("ring %s: %s", cb::unit_name(flat), ok ? "acknowledged" : "FAILED");
}
__attribute__((weak)) cb::DoorResult cb_on_door_decide(UnitId) {
  return cb::DoorResult::NotAllowed;  // fail safe: without an application decision the door stays shut
}
__attribute__((weak)) void cb_on_door_open(UnitId) {}
__attribute__((weak)) void cb_on_door_result(cb::DoorResult, bool, uint32_t) {}

void cb_net_mdns_update_name() {
  const CbProvisioning &p = cb_prov();
  if (!mdns_started || (p.role != UnitId::EG && p.role != UnitId::OG)) return;
  MDNS.addServiceTxt("clearbell", "udp", "name", p.flat_name[(int)p.role]);
}

bool cb_net_send_event(UnitId peer, cb::EventCode code, const uint8_t *data, size_t len) {
  bool ok = engine.send_event(peer, code, data, len, millis());
  apply_sleep();
  return ok;
}

__attribute__((weak)) void cb_on_event(UnitId, cb::EventCode, const uint8_t *, size_t) {}
__attribute__((weak)) void cb_on_event_result(UnitId, cb::EventCode, bool) {}
