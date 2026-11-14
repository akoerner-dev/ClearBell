// test_link.cpp - the protocol engine (cb_link.h) with a simulated network: three units, a shared clock,
// switchable packet loss, and a recorder to replay old packets like an attacker in the WiFi would.
// Covers Softwarearchitektur 11.2: retransmission automaton, duplicates, reboots, replay, wrong receiver.
#include <cstring>
#include <deque>
#include <vector>

#include "cb_link.h"
#include "mini_test.h"

using namespace cb;

namespace {

struct Packet {
  UnitId to;
  std::vector<uint8_t> bytes;
};

struct Net;

struct Node : LinkHandler {
  Net *net = nullptr;
  UnitId id = UnitId::None;
  Link link;
  int rings = 0, ring_ok = 0, ring_fail = 0;
  uint32_t last_result_at = 0;
  bool reachable[4] = {};
  // door, outdoor side
  DoorResult decide = DoorResult::Opened;
  int opened[4] = {};
  // door, indoor side
  int door_results = 0, door_no_answer = 0;
  DoorResult last_door = DoorResult::NotAllowed;
  void link_send(UnitId peer, const uint8_t *frame, size_t len) override;
  void link_ring(UnitId) override { rings++; }
  void link_ring_result(UnitId, bool ok) override;
  void link_peer_state(UnitId peer, bool r) override { reachable[(int)peer] = r; }
  DoorResult link_door_decide(UnitId) override { return decide; }
  void link_door_execute(UnitId from) override { opened[(int)from]++; }
  // events
  int events = 0, event_ok = 0, event_fail = 0;
  EventCode last_code = EventCode::MaintenanceRequest;
  std::vector<uint8_t> last_data;
  void link_event(UnitId, EventCode c, const uint8_t *d, size_t len) override {
    events++;
    last_code = c;
    last_data.assign(d, d + len);
  }
  void link_event_result(UnitId, EventCode, bool ok) override { (ok ? event_ok : event_fail)++; }
  void link_door_result(DoorResult r, bool answered) override {
    if (answered) {
      door_results++;
      last_door = r;
    } else {
      door_no_answer++;
    }
  }
};

struct Net {
  uint32_t now = 1000;
  Node nodes[4];
  std::deque<Packet> queue;
  std::vector<Packet> recorded;  // everything ever sent (the "attacker" in the WiFi)
  bool drop_to[4] = {};          // drop everything addressed to this unit
  int drop_next_from[4] = {};    // drop the next n frames sent by this unit
  uint8_t key_eg[kKeyLen], key_og[kKeyLen];
  uint32_t session_counter = 0x1000;

  Net() {
    for (size_t i = 0; i < kKeyLen; i++) {
      key_eg[i] = (uint8_t)(i * 7 + 1);
      key_og[i] = (uint8_t)(i * 11 + 3);
    }
    for (int i = 1; i < 4; i++) boot((UnitId)i);
  }

  void boot(UnitId u) {
    Node &n = nodes[(int)u];
    n.net = this;
    n.id = u;
    n.link.begin(u, ++session_counter, &n, now);
    if (u == UnitId::Outdoor) {
      n.link.set_key(UnitId::EG, key_eg);
      n.link.set_key(UnitId::OG, key_og);
    } else {
      n.link.set_key(UnitId::Outdoor, u == UnitId::EG ? key_eg : key_og);
    }
    n.link.set_network(true, now);
  }

  // Advances time in 1-ms steps, delivering queued packets (delivery takes one step).
  void run(uint32_t ms) {
    for (uint32_t i = 0; i < ms; i++) {
      now++;
      size_t n = queue.size();
      for (size_t k = 0; k < n; k++) {
        Packet p = queue.front();
        queue.pop_front();
        if (!drop_to[(int)p.to]) nodes[(int)p.to].link.receive(p.bytes.data(), p.bytes.size(), now);
      }
      for (int u = 1; u < 4; u++) nodes[u].link.tick(now);
    }
  }

  void inject(UnitId to, const std::vector<uint8_t> &bytes) {
    nodes[(int)to].link.receive(bytes.data(), bytes.size(), now);
  }
};

void Node::link_send(UnitId peer, const uint8_t *frame, size_t len) {
  Packet p{peer, std::vector<uint8_t>(frame, frame + len)};
  net->recorded.push_back(p);
  if (net->drop_next_from[(int)id] > 0) {
    net->drop_next_from[(int)id]--;
    return;
  }
  net->queue.push_back(p);
}

void Node::link_ring_result(UnitId, bool ok) {
  (ok ? ring_ok : ring_fail)++;
  last_result_at = net->now;
}

Node &AU(Net &n) { return n.nodes[1]; }
Node &EG(Net &n) { return n.nodes[2]; }
Node &OG(Net &n) { return n.nodes[3]; }

// Index of the last recorded frame of a given type sent to `to`.
int last_of(const Net &n, UnitId to, MsgType t) {
  for (int i = (int)n.recorded.size() - 1; i >= 0; i--)
    if (n.recorded[i].to == to && n.recorded[i].bytes[3] == (uint8_t)t) return i;
  return -1;
}

// Index of the first recorded frame of a given type sent to `to` (or -1).
int first_of(const Net &n, UnitId to, MsgType t) {
  for (int i = 0; i < (int)n.recorded.size(); i++)
    if (n.recorded[i].to == to && n.recorded[i].bytes[3] == (uint8_t)t) return i;
  return -1;
}

}  // namespace

TEST(link_replay_window) {
  ReplayWindow w;
  CHECK(w.check(5) == ReplayWindow::Verdict::New);
  w.commit(5);
  CHECK(w.check(5) == ReplayWindow::Verdict::Duplicate);
  CHECK(w.check(4) == ReplayWindow::Verdict::New);  // out of order but inside the window
  w.commit(4);
  CHECK(w.check(4) == ReplayWindow::Verdict::Duplicate);
  w.commit(100);
  CHECK(w.check(37) == ReplayWindow::Verdict::New);     // age 63: still inside
  CHECK(w.check(36) == ReplayWindow::Verdict::TooOld);  // age 64: outside
  CHECK(w.check(5) == ReplayWindow::Verdict::TooOld);
  w.commit(1000);  // jump > 64 clears the history
  CHECK(w.check(999) == ReplayWindow::Verdict::New);
}

TEST(link_nonce_single_use_and_expiry) {
  NonceTable t;
  uint8_t n1[kNonceLen], n2[kNonceLen];
  t.issue(UnitId::EG, NoncePurpose::Ping, 0, n1);
  t.issue(UnitId::EG, NoncePurpose::Ping, 0, n2);
  CHECK(!t.consume(UnitId::OG, NoncePurpose::Ping, n1, 10));    // wrong peer
  CHECK(!t.consume(UnitId::EG, NoncePurpose::Door, n1, 10));    // wrong purpose
  CHECK(t.consume(UnitId::EG, NoncePurpose::Ping, n1, 10));
  CHECK(!t.consume(UnitId::EG, NoncePurpose::Ping, n1, 20));    // used up
  CHECK(!t.consume(UnitId::EG, NoncePurpose::Ping, n2, kNonceLifetimeMs + 1));  // expired
}

TEST(link_boot_establishes_sessions_both_ways) {
  Net n;
  n.run(50);
  CHECK(AU(n).link.session_known(UnitId::EG));
  CHECK(AU(n).link.session_known(UnitId::OG));
  CHECK(EG(n).link.session_known(UnitId::Outdoor));
  CHECK(OG(n).link.session_known(UnitId::Outdoor));
  CHECK(AU(n).reachable[2] && AU(n).reachable[3] && EG(n).reachable[1]);
}

TEST(link_ring_delivered_once_and_acknowledged) {
  Net n;
  n.run(50);
  CHECK(AU(n).link.send_ring(UnitId::EG, n.now));
  n.run(20);
  CHECK_EQ(EG(n).rings, 1);
  CHECK_EQ(OG(n).rings, 0);  // two independent ring paths
  CHECK_EQ(AU(n).ring_ok, 1);
  CHECK_EQ(AU(n).link.stats().tx_retries, 0);
}

TEST(link_lost_ack_gives_retry_duplicate_not_rung_twice) {
  Net n;
  n.run(50);
  n.drop_next_from[2] = 1;  // EG's first RING_ACK is lost
  AU(n).link.send_ring(UnitId::EG, n.now);
  n.run(700);
  CHECK_EQ(EG(n).rings, 1);  // rang once
  CHECK_EQ(AU(n).ring_ok, 1);
  CHECK_EQ(AU(n).ring_fail, 0);
  CHECK_EQ(AU(n).link.stats().tx_retries, 1);
  CHECK_EQ(EG(n).link.stats().rx_duplicate, 1);
}

TEST(link_unreachable_fails_after_1200ms_exactly) {
  Net n;
  n.run(50);
  n.drop_to[2] = true;  // EG switched off
  uint32_t t0 = n.now;
  AU(n).link.send_ring(UnitId::EG, n.now);
  n.run(1199);
  CHECK_EQ(AU(n).ring_fail, 0);  // not decided before the last retry has timed out
  n.run(5);
  CHECK_EQ(AU(n).ring_fail, 1);
  CHECK_EQ(AU(n).last_result_at - t0, 1200);
  CHECK_EQ(AU(n).link.stats().tx_retries, 3);
}

TEST(link_no_network_fails_immediately) {
  Net n;
  n.run(50);
  AU(n).link.set_network(false, n.now);
  CHECK(!AU(n).link.send_ring(UnitId::EG, n.now));
  CHECK(!AU(n).reachable[2]);  // and the peers count as unreachable
}

TEST(link_three_slots_then_immediate_failure) {
  Net n;
  n.run(50);
  n.drop_to[2] = n.drop_to[3] = true;
  CHECK(AU(n).link.send_ring(UnitId::EG, n.now));
  CHECK(AU(n).link.send_ring(UnitId::OG, n.now));
  CHECK(AU(n).link.send_ring(UnitId::EG, n.now));
  CHECK(!AU(n).link.send_ring(UnitId::OG, n.now));
  CHECK_EQ(AU(n).link.stats().ring_no_slot, 1);
}

TEST(link_outdoor_reboot_between_two_rings) {
  Net n;
  n.run(50);
  AU(n).link.send_ring(UnitId::EG, n.now);
  n.run(50);
  n.boot(UnitId::Outdoor);  // new session, seq starts at 1 again - the V0.1 bug scenario
  AU(n).link.send_ring(UnitId::EG, n.now);
  n.run(1500);
  CHECK_EQ(EG(n).rings, 2);    // both rang (V0.1 would have swallowed the second as "duplicate")
  CHECK_EQ(AU(n).ring_ok, 2);  // and both were acknowledged (test counter survives the simulated reboot)
}

TEST(link_ring_right_after_indoor_reboot) {
  Net n;
  n.run(50);
  n.boot(UnitId::EG);  // EG forgot the outdoor session; its boot ping is underway
  AU(n).link.send_ring(UnitId::EG, n.now);
  n.run(1500);
  CHECK_EQ(EG(n).rings, 1);
  CHECK_EQ(AU(n).ring_ok, 1);
}

TEST(link_replayed_ring_same_session_not_rung) {
  Net n;
  n.run(50);
  AU(n).link.send_ring(UnitId::EG, n.now);
  n.run(20);
  int i = last_of(n, UnitId::EG, MsgType::Ring);
  CHECK(i >= 0);
  n.inject(UnitId::EG, n.recorded[i].bytes);  // attacker replays the recorded ring
  n.run(20);
  CHECK_EQ(EG(n).rings, 1);
}

TEST(link_replayed_ring_old_session_not_rung) {
  Net n;
  n.run(50);
  AU(n).link.send_ring(UnitId::EG, n.now);
  n.run(20);
  std::vector<uint8_t> old_ring = n.recorded[last_of(n, UnitId::EG, MsgType::Ring)].bytes;
  n.boot(UnitId::Outdoor);  // outdoor reboots, EG learns the new session
  n.run(50);
  CHECK(EG(n).link.session_known(UnitId::Outdoor));
  int before = EG(n).rings;
  n.inject(UnitId::EG, old_ring);  // a valid tag, but from yesterday's session
  n.run(3000);
  CHECK_EQ(EG(n).rings, before);
  CHECK(EG(n).link.stats().rx_unknown_session >= 1);
}

TEST(link_old_ack_cannot_confirm_new_ring) {
  Net n;
  n.run(50);
  AU(n).link.send_ring(UnitId::EG, n.now);
  n.run(20);
  std::vector<uint8_t> old_ack = n.recorded[last_of(n, UnitId::Outdoor, MsgType::RingAck)].bytes;
  n.boot(UnitId::Outdoor);
  n.run(50);
  n.drop_to[2] = true;  // EG gone - the new ring must fail ...
  int ok_before = AU(n).ring_ok, fail_before = AU(n).ring_fail;
  AU(n).link.send_ring(UnitId::EG, n.now);
  n.inject(UnitId::Outdoor, old_ack);  // ... even if someone replays the old ack (same seq again!)
  n.run(1300);
  CHECK_EQ(AU(n).ring_ok - ok_before, 0);
  CHECK_EQ(AU(n).ring_fail - fail_before, 1);
}

TEST(link_eg_and_og_cannot_talk_or_forge) {
  Net n;
  n.run(50);
  // A frame from OG to EG, correctly tagged with OG's key: EG shares no key with OG.
  uint8_t buf[kMaxFrame];
  Frame f{};
  f.type = MsgType::Ring;
  f.sender = UnitId::OG;
  f.receiver = UnitId::EG;
  f.session = 1;
  f.seq = 1;
  size_t len = encode_frame(f, n.key_og, buf, sizeof(buf));
  CHECK(EG(n).link.receive(buf, len, n.now) == RxError::BadAddress);
  // OG pretending to be the outdoor unit, using ITS key: tag check fails at EG.
  f.sender = UnitId::Outdoor;
  len = encode_frame(f, n.key_og, buf, sizeof(buf));
  CHECK(EG(n).link.receive(buf, len, n.now) == RxError::BadTag);
  // A frame for EG delivered to OG: wrong receiver.
  f.receiver = UnitId::EG;
  len = encode_frame(f, n.key_eg, buf, sizeof(buf));
  CHECK(OG(n).link.receive(buf, len, n.now) == RxError::BadAddress);
  CHECK_EQ(EG(n).rings + OG(n).rings, 0);
}

TEST(link_heartbeat_detects_loss_and_return) {
  Net n;
  n.run(50);
  CHECK(AU(n).reachable[2]);
  n.drop_to[2] = true;
  n.run(kHeartbeatMs * 3 + 100);
  CHECK(AU(n).reachable[2]);  // two misses are tolerated ...
  n.run(kHeartbeatMs + 100);
  CHECK(!AU(n).reachable[2]);  // ... the third makes it unreachable
  CHECK(AU(n).reachable[3]);   // OG unaffected
  n.drop_to[2] = false;
  n.run(kHeartbeatSearchMs + 100);  // searching again at the faster rate
  CHECK(AU(n).reachable[2]);
}

// ---- Door command (Softwarearchitektur 4.4 and 11.2 "Sicherheit") ------------------------------------------

TEST(door_opens_once_and_reports_back) {
  Net n;
  n.run(50);
  CHECK(EG(n).link.send_door_request(n.now));
  n.run(20);
  CHECK_EQ(AU(n).opened[2], 1);
  CHECK_EQ(AU(n).opened[3], 0);
  CHECK_EQ(EG(n).door_results, 1);
  CHECK(EG(n).last_door == DoorResult::Opened);
  CHECK(EG(n).link.send_door_request(n.now));  // a new request is possible again afterwards (RV-38: was "|| true")
  n.run(20);
  CHECK_EQ(AU(n).opened[2], 2);  // ... and the second transaction runs through completely
}

TEST(door_one_transaction_at_a_time) {
  Net n;
  n.run(50);
  n.drop_to[1] = true;
  CHECK(EG(n).link.send_door_request(n.now));
  CHECK(!EG(n).link.send_door_request(n.now));
}

TEST(door_lock_out_answer_is_passed_through) {
  Net n;
  n.run(50);
  AU(n).decide = DoorResult::Locked;
  EG(n).link.send_door_request(n.now);
  n.run(20);
  CHECK_EQ(AU(n).opened[2], 0);
  CHECK(EG(n).last_door == DoorResult::Locked);
}

TEST(door_replayed_confirm_does_not_open_again) {
  Net n;
  n.run(50);
  EG(n).link.send_door_request(n.now);
  n.run(20);
  std::vector<uint8_t> confirm = n.recorded[last_of(n, UnitId::Outdoor, MsgType::DoorConfirm)].bytes;
  n.run(100);
  n.inject(UnitId::Outdoor, confirm);  // same session: duplicate -> old answer repeated, no second opening
  n.run(5000);
  n.inject(UnitId::Outdoor, confirm);  // seconds later: still nothing
  n.run(20);
  CHECK_EQ(AU(n).opened[2], 1);
}

TEST(door_replayed_confirm_after_outdoor_reboot_fails) {
  Net n;
  n.run(50);
  EG(n).link.send_door_request(n.now);
  n.run(20);
  std::vector<uint8_t> confirm = n.recorded[last_of(n, UnitId::Outdoor, MsgType::DoorConfirm)].bytes;
  int before = AU(n).opened[2];
  n.boot(UnitId::Outdoor);
  n.run(50);
  n.inject(UnitId::Outdoor, confirm);  // valid tag, but the session it belongs to is gone
  n.run(3000);
  CHECK_EQ(before, 1);                  // it did open once, legitimately
  CHECK_EQ(AU(n).opened[2], before);    // the replay did not (test counters survive the simulated reboot)
}

TEST(door_late_confirm_after_2s_is_refused) {
  Net n;
  n.run(50);
  // In this simulation every delivery takes 1 ms: request delivered at +1, challenge at +2, and EG's
  // CONFIRM would arrive at +3. Cut the line to the outdoor unit after +2 ...
  EG(n).link.send_door_request(n.now);
  n.run(2);
  n.drop_to[1] = true;  // the CONFIRM and its retries never reach the outdoor unit
  n.run(1500);          // EG gives up
  CHECK_EQ(EG(n).door_no_answer, 1);
  std::vector<uint8_t> confirm = n.recorded[last_of(n, UnitId::Outdoor, MsgType::DoorConfirm)].bytes;
  n.drop_to[1] = false;
  n.run(kNonceLifetimeMs);           // ... and deliver the confirm only after the challenge expired
  n.inject(UnitId::Outdoor, confirm);
  n.run(20);
  CHECK_EQ(AU(n).opened[2], 0);
  CHECK(AU(n).link.stats().door_bad_nonce >= 1);
}

TEST(door_lost_challenge_is_retried_and_opens_once) {
  Net n;
  n.run(50);
  n.drop_next_from[1] = 1;  // the first CHALLENGE is lost
  EG(n).link.send_door_request(n.now);
  n.run(800);
  CHECK_EQ(AU(n).opened[2], 1);
  CHECK(EG(n).last_door == DoorResult::Opened);
  CHECK_EQ(AU(n).link.stats().door_challenges, 2);
}

TEST(door_lost_ack_repeats_answer_without_second_opening) {
  Net n;
  n.run(50);
  EG(n).link.send_door_request(n.now);
  n.run(2);                 // +1 request delivered, +2 challenge delivered, confirm sent
  n.drop_next_from[1] = 1;  // the next outdoor frame is the DOOR_ACK (+3): lost -> EG retransmits
  n.run(800);
  CHECK(AU(n).link.stats().rx_duplicate >= 1);  // the retransmitted CONFIRM really arrived ...
  CHECK_EQ(AU(n).opened[2], 1);                 // ... and did not open a second time
  CHECK_EQ(EG(n).door_results, 1);
  CHECK(EG(n).last_door == DoorResult::Opened);
}

TEST(door_challenge_for_eg_cannot_be_used_by_og) {
  Net n;
  n.run(50);
  n.drop_to[2] = true;  // EG never sees the challenge
  EG(n).link.send_door_request(n.now);
  n.run(5);
  // The attacker (here: the OG unit, which has its own valid key) takes the nonce from the challenge
  // sent to EG and builds a perfectly tagged CONFIRM with OG's key.
  const std::vector<uint8_t> &ch = n.recorded[last_of(n, UnitId::EG, MsgType::DoorChallenge)].bytes;
  uint8_t nonce[kNonceLen];
  memcpy(nonce, ch.data() + kHeaderLen + 8, kNonceLen);
  OG(n).link.send_door_request(n.now);  // gives OG a confirmed context, then forge:
  n.run(1);
  uint8_t buf[kMaxFrame];
  Frame f{};
  f.type = MsgType::DoorConfirm;
  f.sender = UnitId::OG;
  f.receiver = UnitId::Outdoor;
  f.session = OG(n).link.session();
  f.seq = 1000;
  f.payload = nonce;
  f.payload_len = kNonceLen;
  size_t len = encode_frame(f, n.key_og, buf, sizeof(buf));
  n.drop_to[3] = true;  // keep OG's own honest transaction out of the way
  n.inject(UnitId::Outdoor, std::vector<uint8_t>(buf, buf + len));
  n.run(20);
  CHECK_EQ(AU(n).opened[2], 0);
  CHECK_EQ(AU(n).opened[3], 0);  // OG's forged confirm used EG's nonce: refused
}

TEST(door_guessed_nonce_is_refused) {
  Net n;
  n.run(50);
  uint8_t guess[kNonceLen] = {1, 2, 3};
  uint8_t buf[kMaxFrame];
  Frame f{};
  f.type = MsgType::DoorConfirm;
  f.sender = UnitId::EG;
  f.receiver = UnitId::Outdoor;
  f.session = EG(n).link.session();
  f.seq = 5000;
  f.payload = guess;
  f.payload_len = kNonceLen;
  size_t len = encode_frame(f, n.key_eg, buf, sizeof(buf));  // even with the RIGHT key
  n.inject(UnitId::Outdoor, std::vector<uint8_t>(buf, buf + len));
  n.run(20);
  CHECK_EQ(AU(n).opened[2], 0);
  CHECK_EQ(AU(n).link.stats().door_bad_nonce, 1);
}

TEST(door_no_answer_when_outdoor_unreachable) {
  Net n;
  n.run(50);
  n.drop_to[1] = true;
  EG(n).link.send_door_request(n.now);
  n.run(1300);
  CHECK_EQ(EG(n).door_no_answer, 1);
  CHECK_EQ(EG(n).door_results, 0);
}

// ---- Events (port step 10b) ------------------------------------------------------------------------------------

TEST(event_delivered_once_with_data) {
  Net n;
  n.run(50);
  const uint8_t data[] = {1, 2, 3};
  CHECK(EG(n).link.send_event(UnitId::Outdoor, EventCode::PhoneRemove, data, sizeof data, n.now));
  n.run(20);
  CHECK_EQ(AU(n).events, 1);
  CHECK(AU(n).last_code == EventCode::PhoneRemove);
  CHECK(AU(n).last_data.size() == 3 && AU(n).last_data[2] == 3);
  CHECK_EQ(EG(n).event_ok, 1);
}

TEST(event_lost_ack_executes_once) {
  Net n;
  n.run(50);
  n.drop_next_from[1] = 1;  // the outdoor unit's first EVENT_ACK is lost
  EG(n).link.send_event(UnitId::Outdoor, EventCode::MaintenanceRequest, nullptr, 0, n.now);
  n.run(700);
  CHECK_EQ(AU(n).events, 1);
  CHECK_EQ(EG(n).event_ok, 1);
  CHECK_EQ(EG(n).event_fail, 0);
}

TEST(event_replay_and_unreachable) {
  Net n;
  n.run(50);
  EG(n).link.send_event(UnitId::Outdoor, EventCode::MaintenanceRequest, nullptr, 0, n.now);
  n.run(20);
  int i = last_of(n, UnitId::Outdoor, MsgType::Event);
  CHECK(i >= 0);
  n.inject(UnitId::Outdoor, n.recorded[i].bytes);  // recorded event replayed in the same session
  n.run(20);
  CHECK_EQ(AU(n).events, 1);
  std::vector<uint8_t> old = n.recorded[i].bytes;
  n.boot(UnitId::EG);  // after a reboot of the sender the old session is dead
  n.run(50);
  n.inject(UnitId::Outdoor, old);
  n.run(2000);
  CHECK_EQ(AU(n).events, 1);
  n.drop_to[1] = true;  // outdoor off: event fails after the retries
  EG(n).link.send_event(UnitId::Outdoor, EventCode::MaintenanceRequest, nullptr, 0, n.now);
  n.run(1300);
  CHECK_EQ(EG(n).event_fail, 1);
}

TEST(event_rules) {
  Net n;
  n.run(50);
  uint8_t big[kMaxPayload] = {};
  CHECK(!EG(n).link.send_event(UnitId::Outdoor, EventCode::PhoneAdd, big, sizeof big, n.now));  // too long
  CHECK(EG(n).link.send_event(UnitId::Outdoor, EventCode::PhoneAdd, big, kMaxPayload - 1, n.now));
  CHECK(!EG(n).link.send_event(UnitId::OG, EventCode::MaintenanceRequest, nullptr, 0, n.now));  // no key EG<->OG
  // an event does not block the door transaction (and vice versa)
  CHECK(EG(n).link.send_door_request(n.now));
  n.run(50);
  CHECK_EQ(EG(n).door_results, 1);
  CHECK_EQ(AU(n).events, 1);
}

// ---- VK-07_Gegenstellen_Adresse_haerten (security catalogue 13.11.2026) ---------------------------------------

// RV-34: the challenge arrives only after EG already repeated its request (modem sleep, > 300 ms). The repeated
// request must get the SAME challenge again - not a new one that kills the one EG is about to confirm.
TEST(door_late_challenge_still_opens) {
  Net n;
  n.run(50);
  n.drop_next_from[1] = 2;  // the challenge and the answer to the repeated request are both "still in the air"
  EG(n).link.send_door_request(n.now);
  n.run(kAckTimeoutMs + 10);  // EG has repeated its request by now
  int first = first_of(n, UnitId::EG, MsgType::DoorChallenge);
  CHECK(first >= 0);
  n.inject(UnitId::EG, n.recorded[first].bytes);  // the FIRST challenge finally arrives
  n.run(50);
  CHECK_EQ(AU(n).opened[2], 1);
  CHECK(EG(n).last_door == DoorResult::Opened);
  CHECK_EQ(AU(n).link.stats().door_bad_nonce, 0u);
}

// RV-34: a recorded DOOR_REQUEST played in during a running transaction must not revoke the valid challenge.
TEST(door_replayed_request_during_transaction_does_not_break_it) {
  Net n;
  n.run(50);
  EG(n).link.send_door_request(n.now);
  n.run(1);  // request delivered, the challenge is queued
  std::vector<uint8_t> req = n.recorded[last_of(n, UnitId::Outdoor, MsgType::DoorRequest)].bytes;
  n.inject(UnitId::Outdoor, req);
  n.inject(UnitId::Outdoor, req);
  n.run(50);
  CHECK_EQ(AU(n).opened[2], 1);
  CHECK(EG(n).last_door == DoorResult::Opened);
  CHECK_EQ(AU(n).link.stats().door_bad_nonce, 0u);
}

// RV-34: after the transaction, replaying its request starts nothing (no new challenge goes out).
TEST(door_replayed_request_after_transaction_issues_no_challenge) {
  Net n;
  n.run(50);
  EG(n).link.send_door_request(n.now);
  n.run(50);
  CHECK_EQ(AU(n).opened[2], 1);
  uint32_t challenges = AU(n).link.stats().door_challenges;
  std::vector<uint8_t> req = n.recorded[last_of(n, UnitId::Outdoor, MsgType::DoorRequest)].bytes;
  n.inject(UnitId::Outdoor, req);
  n.run(3000);
  n.inject(UnitId::Outdoor, req);
  n.run(50);
  CHECK_EQ(AU(n).link.stats().door_challenges, challenges);
  CHECK_EQ(AU(n).opened[2], 1);
}

// RV-10: only fresh authentic frames may teach the device glue a peer address. Duplicates, too-old frames and a
// replayed PONG are authentic (valid tag) but NOT fresh.
TEST(link_only_fresh_frames_count_as_fresh) {
  Net n;
  n.run(50);
  uint32_t f0 = EG(n).link.stats().rx_fresh;
  CHECK(f0 > 0);  // the session setup (PONG on our own nonce) was fresh
  CHECK(AU(n).link.send_ring(UnitId::EG, n.now));
  n.run(10);
  CHECK_EQ(EG(n).rings, 1);
  uint32_t f1 = EG(n).link.stats().rx_fresh;
  CHECK(f1 > f0);  // a new ring in a confirmed session is fresh
  std::vector<uint8_t> ring = n.recorded[last_of(n, UnitId::EG, MsgType::Ring)].bytes;
  n.inject(UnitId::EG, ring);  // duplicate: authentic, acknowledged again, but not fresh
  CHECK(!EG(n).link.last_rx_fresh());
  CHECK_EQ(EG(n).link.stats().rx_fresh, f1);
  int pong = last_of(n, UnitId::EG, MsgType::Pong);
  CHECK(pong >= 0);
  uint32_t a0 = AU(n).link.stats().rx_fresh;
  n.inject(UnitId::Outdoor, n.recorded[last_of(n, UnitId::Outdoor, MsgType::Pong)].bytes);  // nonce used up
  CHECK(!AU(n).link.last_rx_fresh());
  CHECK_EQ(AU(n).link.stats().rx_fresh, a0);
  n.run(2 * kHeartbeatMs);  // many rings later the old ring is far outside the window: still not fresh
  for (int i = 0; i < 70; i++) {
    AU(n).link.send_ring(UnitId::EG, n.now);
    n.run(5);
  }
  uint32_t f2 = EG(n).link.stats().rx_fresh;
  n.inject(UnitId::EG, ring);
  CHECK(!EG(n).link.last_rx_fresh());
  CHECK_EQ(EG(n).link.stats().rx_fresh, f2);
}

// RV-38 c): two lost challenges in a row - the third try still opens exactly once, within the nonce lifetime.
TEST(door_two_lost_challenges_still_open_once) {
  Net n;
  n.run(50);
  n.drop_next_from[1] = 2;
  EG(n).link.send_door_request(n.now);
  n.run(kNonceLifetimeMs);
  CHECK_EQ(AU(n).opened[2], 1);
  CHECK(EG(n).last_door == DoorResult::Opened);
  CHECK_EQ(AU(n).link.stats().door_bad_nonce, 0u);
}

// RV-38 d): the rule cb_net uses for learning a peer address, written down where it can be tested.
TEST(link_address_learning_rule) {
  CHECK(should_learn_address(RxError::Ok, true, false));
  CHECK(!should_learn_address(RxError::Ok, false, false));    // authentic but replayed / duplicate
  CHECK(!should_learn_address(RxError::BadTag, true, false));  // not authentic
  CHECK(!should_learn_address(RxError::Ok, true, true));       // an address fixed by provisioning is never replaced
}
