// cb_link.h - the protocol engine between the units: sessions, duplicates, acknowledgements,
// retransmission and heartbeat (Softwarearchitektur 4.3). Hardware-free; the device glue (UDP, WiFi) is
// main/cb_net.cpp, the PC tests are test/host/test_link.cpp.
//
// HOW IT WORKS, in short
//
// * Session: every unit draws a random 32-bit session number at boot. A peer's messages are only acted
//   on once its CURRENT session is confirmed - otherwise a recorded packet from yesterday would still be
//   valid. Confirmation = challenge: I send PING with a fresh nonce, the peer answers PONG with that nonce
//   inside its session. Then I accept that session. After a reboot both sides do this immediately.
// * Duplicates: within a confirmed session a 64-entry sliding window (as in IPsec) remembers which
//   sequence numbers were processed. A duplicate is NOT executed again but IS acknowledged again - the
//   usual reason for a duplicate is a lost acknowledgement.
// * Acknowledgements carry the session AND seq of the message they confirm. That binds an ack to exactly
//   one message of the current boot of the sender; an old recorded ack can never confirm a new ring.
// * Retransmission: a ring is sent, then repeated every 300 ms up to 3 times; no ack after 1.2 s = failed.
//   3 send slots, so ringing EG and OG can overlap. All slots busy or no WiFi = immediate failure.
// * Ack before action: a receiver sends the ack first and then reports the event to the application.
// * Heartbeat: PING every 30 s (every 5 s until first contact). Three unanswered pings in a row = peer
//   unreachable (status LED red), so the outdoor unit knows before the next ring.
//
// * Door command (indoor -> outdoor, Softwarearchitektur 4.4): DOOR_REQUEST -> DOOR_CHALLENGE (fresh
//   nonce, 2 s, single use, only the newest per flat is valid) -> DOOR_CONFIRM (the nonce, tagged with the
//   flat's key) -> DOOR_ACK (Opened / Locked / BadNonce / NotAllowed). The outdoor unit decides, sends the
//   ack, THEN hands over to the door task. A repeated CONFIRM gets the stored answer again, never a second
//   opening; a repeated REQUEST gets the SAME challenge again while it is valid, never a new one (RV-34).
//   Each phase is retransmitted like a ring.
//
// Messages are only processed after verify_frame() succeeded (right key, tag ok). Everything that fails
// is dropped silently and counted.
#pragma once

#include <cstddef>
#include <cstdint>

#include "cb_protocol.h"

namespace cb {

// ---- Timing (Softwarearchitektur 4.3) ----------------------------------------------------------------
constexpr uint32_t kAckTimeoutMs = 300;
constexpr int kMaxRetries = 3;
constexpr int kSendSlots = 3;
constexpr uint32_t kHeartbeatMs = 30000;
constexpr uint32_t kHeartbeatSearchMs = 5000;  // until the peer answered for the first time
constexpr int kMissedForUnreachable = 3;
constexpr uint32_t kNonceLifetimeMs = 2000;    // a challenge is valid this long, once

// ---- Sliding window against replay and duplicates ----------------------------------------------------
class ReplayWindow {
 public:
  enum class Verdict : uint8_t { New, Duplicate, TooOld };
  void reset() { valid_ = false; }
  Verdict check(uint32_t seq) const;
  void commit(uint32_t seq);

 private:
  bool valid_ = false;
  uint32_t highest_ = 0;
  uint64_t bits_ = 0;  // bit i = seq (highest_ - i) was processed
};

// ---- Outstanding challenges ---------------------------------------------------------------------------
enum class NoncePurpose : uint8_t { Ping, Door };

class NonceTable {
 public:
  static constexpr int kEntries = 8;
  // Creates a fresh random nonce for `peer`; the oldest entry is overwritten when the table is full.
  void issue(UnitId peer, NoncePurpose purpose, uint32_t now_ms, uint8_t out[kNonceLen]);
  // True exactly once for a nonce that was issued to `peer` for `purpose` within kNonceLifetimeMs.
  // Comparison in constant time.
  bool consume(UnitId peer, NoncePurpose purpose, const uint8_t *nonce, uint32_t now_ms);
  bool pending(UnitId peer, NoncePurpose purpose, uint32_t now_ms) const;
  // Invalidates every outstanding nonce of `peer` for `purpose`. Used for the door: only the NEWEST
  // challenge to a flat is valid, and once one is used, none of the older ones can open a second time.
  void revoke(UnitId peer, NoncePurpose purpose);

 private:
  struct Entry {
    bool used = true;
    UnitId peer = UnitId::None;
    NoncePurpose purpose = NoncePurpose::Ping;
    uint32_t created = 0;
    uint8_t nonce[kNonceLen] = {};
  };
  Entry e_[kEntries];
};

// ---- What the engine tells the application ------------------------------------------------------------
class LinkHandler {
 public:
  virtual ~LinkHandler() = default;
  // Put this frame on the wire to `peer` (UDP). Must not block.
  virtual void link_send(UnitId peer, const uint8_t *frame, size_t len) = 0;
  // A ring for this unit arrived (indoor). The ack is already on its way.
  virtual void link_ring(UnitId from) { (void)from; }
  // Final outcome of a send_ring(): acknowledged (ok) or given up after the retries (or no WiFi).
  virtual void link_ring_result(UnitId to, bool ok) { (void)to; (void)ok; }
  // Reachability of a peer changed (heartbeat).
  virtual void link_peer_state(UnitId peer, bool reachable) { (void)peer; (void)reachable; }

  // ---- Door command (Softwarearchitektur 4.4) ----
  // Outdoor: a flat proved its key with a fresh challenge. Decide NOW (lock-out time, mode) - the answer
  // goes into the DoorAck. Default: refuse. Must not block.
  virtual DoorResult link_door_decide(UnitId from) { (void)from; return DoorResult::NotAllowed; }
  // Outdoor: called right AFTER the DoorAck(Opened) was sent - hand the opening to the door task.
  virtual void link_door_execute(UnitId from) { (void)from; }
  // Indoor: outcome of send_door_request(). `answered` false = no answer at all (result undefined).
  virtual void link_door_result(DoorResult r, bool answered) { (void)r; (void)answered; }

  // ---- Events (port step 10b): authenticated, acknowledged, executed once ----
  // An event from `from` arrived (the ack is already on its way). data/len: payload after the code byte.
  virtual void link_event(UnitId from, EventCode code, const uint8_t *data, size_t len) {
    (void)from, (void)code, (void)data, (void)len;
  }
  // Final outcome of a send_event().
  virtual void link_event_result(UnitId to, EventCode code, bool ok) { (void)to, (void)code, (void)ok; }
};

struct LinkStats {
  uint32_t rx_ok, rx_dropped[6];  // indexed by RxError (only meaningful entries counted)
  uint32_t rx_unknown_session, rx_duplicate, rx_too_old, rx_unexpected;
  uint32_t tx_frames, tx_retries, ring_ok, ring_failed, ring_no_slot;
  uint32_t sessions_accepted;
  uint32_t door_challenges, door_opened, door_refused, door_bad_nonce, door_failed;
  uint32_t events_ok, events_failed, events_rx;
  uint32_t rx_fresh;  // authentic AND fresh frames (VK-07): only these may teach the device glue an address
};

// ---- The engine -----------------------------------------------------------------------------------------
class Link {
 public:
  // `self`: this unit. `session`: random, non-zero, new at every boot (the caller draws it with
  // random_bytes so this class stays deterministic in tests).
  void begin(UnitId self, uint32_t session, LinkHandler *handler, uint32_t now_ms);
  // Link key to `peer` (kKeyLen bytes). Only peers with a key exist for this unit.
  void set_key(UnitId peer, const uint8_t *key);
  // WiFi state. While down, nothing is sent and send_ring() fails at once.
  void set_network(bool up, uint32_t now_ms);

  // One received datagram. Returns why it was dropped, or Ok.
  RxError receive(const uint8_t *buf, size_t len, uint32_t now_ms);
  // Was the frame just received not only authentic but FRESH (VK-07, RV-10)? Fresh = the answer to a nonce or
  // a sequence number this unit issued in this boot (PONG, acks, challenge), or a NEW message inside a confirmed
  // session. A recording (old session, duplicate, too old, replayed PONG) is authentic but not fresh - learning
  // the peer's address from it would let anyone in the WiFi redirect the link with one replayed packet.
  bool last_rx_fresh() const { return last_rx_fresh_; }
  // Starts ringing at `peer`. Returns false if it failed immediately (no WiFi, no key, no free slot);
  // otherwise link_ring_result() follows within ~1.2 s.
  bool send_ring(UnitId peer, uint32_t now_ms);
  // Indoor: asks the outdoor unit to open the door (REQUEST -> CHALLENGE -> CONFIRM -> ACK). Returns false
  // if it failed at once (no WiFi, transaction already running); otherwise link_door_result() follows.
  // Each phase is retransmitted like a ring (3 x 300 ms).
  bool send_door_request(uint32_t now_ms);
  // Sends an event to `peer` (data up to kMaxPayload - 1 bytes). false = failed at once (no WiFi, no key, no
  // slot, too long); otherwise link_event_result() follows within ~1.2 s.
  bool send_event(UnitId peer, EventCode code, const uint8_t *data, size_t len, uint32_t now_ms);
  // Call often (every loop pass): retransmissions, timeouts, heartbeats.
  void tick(uint32_t now_ms);
  // The address of `peer` just became known (mDNS): send the next ping at once instead of waiting for
  // the heartbeat, so a session after a reboot stands within one round trip.
  void ping_soon(UnitId peer, uint32_t now_ms) { peers_[(int)peer].next_ping = now_ms; }

  bool reachable(UnitId peer) const { return peers_[(int)peer].reachable; }
  bool has_key(UnitId peer) const { return peers_[(int)peer].has_key; }
  // True while at least one transaction waits for its answer (modem sleep is kept off meanwhile).
  bool ring_in_flight() const {
    for (const Slot &s : slots_)
      if (s.busy) return true;
    return false;
  }
  bool session_known(UnitId peer) const { return peers_[(int)peer].session_known; }
  uint32_t session() const { return session_; }
  const LinkStats &stats() const { return stats_; }

 private:
  struct Peer {
    bool has_key = false;
    uint8_t key[kKeyLen] = {};
    bool session_known = false;
    uint32_t session = 0;
    ReplayWindow window;
    bool reachable = false;
    bool ever_answered = false;
    int missed = 0;
    bool ping_outstanding = false;
    uint32_t next_ping = 0;
    // Outdoor: the last DOOR_CONFIRM of this flat and what was answered, so a retransmitted confirm (its
    // ack got lost) gets the SAME answer again instead of being evaluated - and executed - a second time.
    bool has_last_confirm = false;
    uint32_t last_confirm_seq = 0;
    DoorResult last_confirm_result = DoorResult::NotAllowed;
    // Outdoor: the challenge issued for the flat's newest DOOR_REQUEST (RV-34). A repeated request with the SAME
    // seq gets this challenge again while it is valid - a new one would revoke the nonce the indoor unit is
    // about to confirm (challenge late > 300 ms, or a replayed request).
    bool has_door_challenge = false;
    uint32_t door_req_seq = 0;
    uint8_t door_nonce[kNonceLen] = {};
  };
  enum class SlotKind : uint8_t { Ring, DoorRequest, DoorConfirm, Event };
  struct Slot {
    bool busy = false;
    SlotKind kind = SlotKind::Ring;
    EventCode code = EventCode::MaintenanceRequest;  // kind Event only
    UnitId peer = UnitId::None;
    uint32_t seq = 0;
    uint8_t frame[kMaxFrame] = {};
    size_t len = 0;
    int retries = 0;
    uint32_t next_at = 0;
  };

  size_t build(UnitId peer, MsgType type, const uint8_t *payload, size_t plen, uint8_t *out, uint32_t *seq_out);
  void send_simple(UnitId peer, MsgType type, const uint8_t *payload, size_t plen);
  void send_ping(UnitId peer, uint32_t now_ms);
  void on_frame(const Frame &f, uint32_t now_ms);
  void on_door_frame(const Frame &f, bool session_ok, uint32_t now_ms);
  void heard_from(UnitId peer);
  void mark_fresh();
  void send_door_ack(UnitId peer, uint32_t confirm_seq, DoorResult r);

  UnitId self_ = UnitId::None;
  uint32_t session_ = 0;
  uint32_t next_seq_ = 1;
  bool network_ = false;
  bool last_rx_fresh_ = false;
  LinkHandler *h_ = nullptr;
  Peer peers_[4];
  Slot slots_[kSendSlots];
  NonceTable nonces_;
  LinkStats stats_ = {};
};

}  // namespace cb
