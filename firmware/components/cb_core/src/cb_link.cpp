// cb_link.cpp - see cb_link.h for the concept. Time comparisons use signed differences of unsigned
// millisecond values ("due(now, t)"), which stays correct across the 32-bit wrap after 49 days.
#include "cb_link.h"

#include <cstring>

#include "cb_bytes.h"
#include "cb_crypto.h"

namespace cb {

namespace {
bool due(uint32_t now, uint32_t at) {
  return (int32_t)(now - at) >= 0;
}
}  // namespace

// ---- ReplayWindow ------------------------------------------------------------------------------------

ReplayWindow::Verdict ReplayWindow::check(uint32_t seq) const {
  if (!valid_ || seq > highest_) return Verdict::New;
  uint32_t age = highest_ - seq;
  if (age >= 64) return Verdict::TooOld;
  return (bits_ >> age) & 1 ? Verdict::Duplicate : Verdict::New;
}

void ReplayWindow::commit(uint32_t seq) {
  if (!valid_) {
    valid_ = true;
    highest_ = seq;
    bits_ = 1;
    return;
  }
  if (seq > highest_) {
    uint32_t shift = seq - highest_;
    bits_ = shift >= 64 ? 0 : bits_ << shift;
    bits_ |= 1;
    highest_ = seq;
  } else {
    uint32_t age = highest_ - seq;
    if (age < 64) bits_ |= (uint64_t)1 << age;
  }
}

// ---- NonceTable ----------------------------------------------------------------------------------------

void NonceTable::issue(UnitId peer, NoncePurpose purpose, uint32_t now_ms, uint8_t out[kNonceLen]) {
  // Reuse a used/expired entry; if none, overwrite the oldest one.
  Entry *slot = nullptr;
  for (Entry &e : e_) {
    if (e.used || now_ms - e.created > kNonceLifetimeMs) {
      slot = &e;
      break;
    }
  }
  if (!slot) {
    slot = &e_[0];
    for (Entry &e : e_)
      if (now_ms - e.created > now_ms - slot->created) slot = &e;
  }
  random_bytes(slot->nonce, kNonceLen);
  slot->used = false;
  slot->peer = peer;
  slot->purpose = purpose;
  slot->created = now_ms;
  memcpy(out, slot->nonce, kNonceLen);
}

bool NonceTable::consume(UnitId peer, NoncePurpose purpose, const uint8_t *nonce, uint32_t now_ms) {
  for (Entry &e : e_) {
    if (e.used || e.peer != peer || e.purpose != purpose || now_ms - e.created > kNonceLifetimeMs) continue;
    if (ct_equal(e.nonce, nonce, kNonceLen)) {
      e.used = true;  // single use
      return true;
    }
  }
  return false;
}

void NonceTable::revoke(UnitId peer, NoncePurpose purpose) {
  for (Entry &e : e_)
    if (e.peer == peer && e.purpose == purpose) e.used = true;
}

bool NonceTable::pending(UnitId peer, NoncePurpose purpose, uint32_t now_ms) const {
  for (const Entry &e : e_)
    if (!e.used && e.peer == peer && e.purpose == purpose && now_ms - e.created <= kNonceLifetimeMs) return true;
  return false;
}

// ---- Link ----------------------------------------------------------------------------------------------

void Link::begin(UnitId self, uint32_t session, LinkHandler *handler, uint32_t now_ms) {
  self_ = self;
  session_ = session ? session : 1;
  next_seq_ = 1;
  h_ = handler;
  for (Peer &p : peers_) p = Peer();
  for (Slot &s : slots_) s = Slot();
  stats_ = LinkStats();
  network_ = false;
  (void)now_ms;
}

void Link::set_key(UnitId peer, const uint8_t *key) {
  Peer &p = peers_[(int)peer];
  memcpy(p.key, key, kKeyLen);
  p.has_key = peer != self_ && peer != UnitId::None;
}

void Link::set_network(bool up, uint32_t now_ms) {
  if (up == network_) return;
  network_ = up;
  for (int i = 1; i < 4; i++) {
    Peer &p = peers_[i];
    if (!p.has_key) continue;
    if (up) {
      // Back online: announce ourselves at once (this also re-confirms sessions after a reboot).
      p.next_ping = now_ms;
      p.ping_outstanding = false;
      p.missed = 0;
    } else if (p.reachable) {
      p.reachable = false;
      p.ever_answered = false;
      if (h_) h_->link_peer_state((UnitId)i, false);
    }
  }
}

size_t Link::build(UnitId peer, MsgType type, const uint8_t *payload, size_t plen, uint8_t *out, uint32_t *seq_out) {
  Frame f{};
  f.type = type;
  f.sender = self_;
  f.receiver = peer;
  f.session = session_;
  f.seq = next_seq_++;
  f.payload = payload;
  f.payload_len = plen;
  if (seq_out) *seq_out = f.seq;
  return encode_frame(f, peers_[(int)peer].key, out, kMaxFrame);
}

void Link::send_simple(UnitId peer, MsgType type, const uint8_t *payload, size_t plen) {
  if (!network_) return;
  uint8_t buf[kMaxFrame];
  size_t n = build(peer, type, payload, plen, buf, nullptr);
  if (n && h_) {
    h_->link_send(peer, buf, n);
    stats_.tx_frames++;
  }
}

void Link::send_ping(UnitId peer, uint32_t now_ms) {
  uint8_t nonce[kNonceLen];
  nonces_.issue(peer, NoncePurpose::Ping, now_ms, nonce);
  send_simple(peer, MsgType::Ping, nonce, kNonceLen);
}

void Link::heard_from(UnitId peer) {
  Peer &p = peers_[(int)peer];
  p.missed = 0;
  p.ping_outstanding = false;
  p.ever_answered = true;
  if (!p.reachable) {
    p.reachable = true;
    if (h_) h_->link_peer_state(peer, true);
  }
}

RxError Link::receive(const uint8_t *buf, size_t len, uint32_t now_ms) {
  UnitId sender, receiver;
  RxError e = precheck_frame(buf, len, &sender, &receiver);
  // Only frames for me, from a unit I share a key with (EG and OG share none, so they cannot talk).
  if (e == RxError::Ok && (receiver != self_ || !peers_[(int)sender].has_key)) e = RxError::BadAddress;
  Frame f;
  if (e == RxError::Ok) e = verify_frame(buf, len, peers_[(int)sender].key, &f);
  if (e != RxError::Ok) {
    stats_.rx_dropped[(int)e]++;
    return e;
  }
  stats_.rx_ok++;
  on_frame(f, now_ms);
  return RxError::Ok;
}

void Link::on_frame(const Frame &f, uint32_t now_ms) {
  Peer &p = peers_[(int)f.sender];
  bool session_ok = p.session_known && f.session == p.session;

  switch (f.type) {
    case MsgType::Ping: {
      if (f.payload_len != kNonceLen) break;
      // Answering is always safe: the PONG only proves *my* liveness for *their* nonce.
      send_simple(f.sender, MsgType::Pong, f.payload, kNonceLen);
      // A ping from an unknown session (peer rebooted?) - challenge it, unless a challenge is out already.
      if (!session_ok && !nonces_.pending(f.sender, NoncePurpose::Ping, now_ms)) send_ping(f.sender, now_ms);
      return;
    }
    case MsgType::Pong: {
      if (f.payload_len != kNonceLen || !nonces_.consume(f.sender, NoncePurpose::Ping, f.payload, now_ms)) break;
      // The peer answered MY fresh nonce inside session f.session: that session is live. Accept it.
      if (!session_ok) {
        p.session_known = true;
        p.session = f.session;
        p.window.reset();
        stats_.sessions_accepted++;
      }
      p.window.commit(f.seq);
      heard_from(f.sender);
      return;
    }
    case MsgType::Ring: {
      if (self_ == UnitId::Outdoor || f.sender != UnitId::Outdoor || f.payload_len != 0) break;
      if (!session_ok) {
        // Not executed and not acknowledged: could be a recording. Challenge the session; the sender's
        // retransmission 300 ms later then gets through.
        stats_.rx_unknown_session++;
        if (!nonces_.pending(f.sender, NoncePurpose::Ping, now_ms)) send_ping(f.sender, now_ms);
        return;
      }
      ReplayWindow::Verdict v = p.window.check(f.seq);
      if (v == ReplayWindow::Verdict::TooOld) {
        stats_.rx_too_old++;
        return;
      }
      uint8_t ack[8];
      put_u32le(ack, f.session);
      put_u32le(ack + 4, f.seq);
      send_simple(f.sender, MsgType::RingAck, ack, sizeof(ack));  // ack FIRST ...
      if (v == ReplayWindow::Verdict::Duplicate) {
        stats_.rx_duplicate++;  // ... a duplicate is acknowledged again but not rung again
        return;
      }
      p.window.commit(f.seq);
      if (h_) h_->link_ring(f.sender);  // ... then the action
      return;
    }
    case MsgType::RingAck: {
      if (f.payload_len != 8 || get_u32le(f.payload) != session_) break;
      uint32_t acked = get_u32le(f.payload + 4);
      for (Slot &s : slots_) {
        if (s.busy && s.kind == SlotKind::Ring && s.peer == f.sender && s.seq == acked) {
          s.busy = false;
          stats_.ring_ok++;
          heard_from(f.sender);
          if (h_) h_->link_ring_result(f.sender, true);
          return;
        }
      }
      stats_.rx_duplicate++;  // late second ack of an already confirmed ring
      return;
    }
    case MsgType::DoorRequest:
    case MsgType::DoorChallenge:
    case MsgType::DoorConfirm:
    case MsgType::DoorAck:
      on_door_frame(f, session_ok, now_ms);
      return;
    case MsgType::Event: {
      // Same rules as a Ring: only in a confirmed session, ack first, a duplicate is acked but not executed.
      if (f.payload_len < 1 || (self_ != UnitId::Outdoor && f.sender != UnitId::Outdoor)) break;
      if (!session_ok) {
        stats_.rx_unknown_session++;
        if (!nonces_.pending(f.sender, NoncePurpose::Ping, now_ms)) send_ping(f.sender, now_ms);
        return;
      }
      ReplayWindow::Verdict v = p.window.check(f.seq);
      if (v == ReplayWindow::Verdict::TooOld) {
        stats_.rx_too_old++;
        return;
      }
      uint8_t ack[8];
      put_u32le(ack, f.session);
      put_u32le(ack + 4, f.seq);
      send_simple(f.sender, MsgType::EventAck, ack, sizeof(ack));
      if (v == ReplayWindow::Verdict::Duplicate) {
        stats_.rx_duplicate++;
        return;
      }
      p.window.commit(f.seq);
      stats_.events_rx++;
      if (h_) h_->link_event(f.sender, (EventCode)f.payload[0], f.payload + 1, f.payload_len - 1);
      return;
    }
    case MsgType::EventAck: {
      if (f.payload_len != 8 || get_u32le(f.payload) != session_) break;
      uint32_t acked = get_u32le(f.payload + 4);
      for (Slot &s : slots_) {
        if (s.busy && s.kind == SlotKind::Event && s.peer == f.sender && s.seq == acked) {
          s.busy = false;
          stats_.events_ok++;
          heard_from(f.sender);
          if (h_) h_->link_event_result(f.sender, s.code, true);
          return;
        }
      }
      stats_.rx_duplicate++;
      return;
    }
  }
  stats_.rx_unexpected++;
}

// ---- Door command: REQUEST -> CHALLENGE -> CONFIRM -> ACK (Softwarearchitektur 4.4) -----------------------
//
// Why this is safe against a recording attacker: the CONFIRM carries a nonce that the outdoor unit drew a
// moment ago from the hardware RNG, valid 2 s and once. A recorded CONFIRM contains an old nonce -> BadNonce.
// The tag proves the flat's key; EG's key cannot produce a valid CONFIRM for OG and vice versa. No counter
// is stored anywhere, so re-flashing a unit cannot desynchronise anything (the V0.1 weakness).

void Link::send_door_ack(UnitId peer, uint32_t confirm_seq, DoorResult r) {
  uint8_t ack[9];
  put_u32le(ack, peers_[(int)peer].session);
  put_u32le(ack + 4, confirm_seq);
  ack[8] = (uint8_t)r;
  send_simple(peer, MsgType::DoorAck, ack, sizeof(ack));
}

void Link::on_door_frame(const Frame &f, bool session_ok, uint32_t now_ms) {
  Peer &p = peers_[(int)f.sender];
  bool i_am_outdoor = self_ == UnitId::Outdoor;

  if (f.type == MsgType::DoorRequest || f.type == MsgType::DoorConfirm) {
    // Outdoor side. Same session rule as for a ring: only act on a confirmed session of the flat.
    if (!i_am_outdoor) {
      stats_.rx_unexpected++;
      return;
    }
    if (!session_ok) {
      stats_.rx_unknown_session++;
      if (!nonces_.pending(f.sender, NoncePurpose::Ping, now_ms)) send_ping(f.sender, now_ms);
      return;
    }
    ReplayWindow::Verdict v = p.window.check(f.seq);
    if (v == ReplayWindow::Verdict::TooOld) {
      stats_.rx_too_old++;
      return;
    }

    if (f.type == MsgType::DoorRequest) {
      if (f.payload_len != 0) {
        stats_.rx_unexpected++;
        return;
      }
      if (v == ReplayWindow::Verdict::New) p.window.commit(f.seq);
      else stats_.rx_duplicate++;
      // New or repeated request (our challenge got lost): issue a FRESH challenge; older ones die with it.
      uint8_t payload[8 + kNonceLen];
      put_u32le(payload, f.session);
      put_u32le(payload + 4, f.seq);
      nonces_.revoke(f.sender, NoncePurpose::Door);
      nonces_.issue(f.sender, NoncePurpose::Door, now_ms, payload + 8);
      send_simple(f.sender, MsgType::DoorChallenge, payload, sizeof(payload));
      stats_.door_challenges++;
      return;
    }

    // DoorConfirm
    if (f.payload_len != kNonceLen) {
      stats_.rx_unexpected++;
      return;
    }
    if (v == ReplayWindow::Verdict::Duplicate) {
      stats_.rx_duplicate++;
      // Our ack got lost: repeat the answer we gave - never evaluate (or open) a second time.
      if (p.has_last_confirm && p.last_confirm_seq == f.seq) send_door_ack(f.sender, f.seq, p.last_confirm_result);
      return;
    }
    p.window.commit(f.seq);
    DoorResult r;
    if (nonces_.consume(f.sender, NoncePurpose::Door, f.payload, now_ms)) {
      nonces_.revoke(f.sender, NoncePurpose::Door);
      r = h_ ? h_->link_door_decide(f.sender) : DoorResult::NotAllowed;
    } else {
      r = DoorResult::BadNonce;  // unknown, expired, already used, or for the other flat
      stats_.door_bad_nonce++;
    }
    p.has_last_confirm = true;
    p.last_confirm_seq = f.seq;
    p.last_confirm_result = r;
    send_door_ack(f.sender, f.seq, r);  // the answer FIRST ...
    if (r == DoorResult::Opened) {
      stats_.door_opened++;
      if (h_) h_->link_door_execute(f.sender);  // ... then the door task takes over
    } else if (r != DoorResult::BadNonce) {
      stats_.door_refused++;
    }
    return;
  }

  // Indoor side: answers to our own request. Both are bound to our session + seq, which makes them fresh;
  // the tag makes them authentic. No session check needed (same reasoning as for RingAck).
  if (i_am_outdoor) {
    stats_.rx_unexpected++;
    return;
  }
  if (f.type == MsgType::DoorChallenge && f.payload_len == 8 + kNonceLen && get_u32le(f.payload) == session_) {
    uint32_t req_seq = get_u32le(f.payload + 4);
    for (Slot &s : slots_) {
      if (s.busy && s.kind == SlotKind::DoorRequest && s.seq == req_seq) {
        // Answer the challenge: the CONFIRM replaces the request in the same slot, retries start anew.
        s.len = build(f.sender, MsgType::DoorConfirm, f.payload + 8, kNonceLen, s.frame, &s.seq);
        s.kind = SlotKind::DoorConfirm;
        s.retries = 0;
        s.next_at = now_ms + kAckTimeoutMs;
        if (s.len && h_) {
          h_->link_send(f.sender, s.frame, s.len);
          stats_.tx_frames++;
        }
        heard_from(f.sender);
        return;
      }
    }
    stats_.rx_duplicate++;  // a second challenge for a request we already answered: ignore
    return;
  }
  if (f.type == MsgType::DoorAck && f.payload_len == 9 && get_u32le(f.payload) == session_) {
    uint32_t confirm_seq = get_u32le(f.payload + 4);
    for (Slot &s : slots_) {
      if (s.busy && s.kind == SlotKind::DoorConfirm && s.seq == confirm_seq) {
        s.busy = false;
        heard_from(f.sender);
        if (h_) h_->link_door_result((DoorResult)f.payload[8], true);
        return;
      }
    }
    stats_.rx_duplicate++;
    return;
  }
  stats_.rx_unexpected++;
}

bool Link::send_door_request(uint32_t now_ms) {
  if (!network_ || self_ == UnitId::Outdoor || !peers_[(int)UnitId::Outdoor].has_key) {
    stats_.door_failed++;
    return false;
  }
  for (const Slot &s : slots_) {
    if (s.busy && (s.kind == SlotKind::DoorRequest || s.kind == SlotKind::DoorConfirm))
      return false;  // one door transaction at a time
  }
  for (Slot &s : slots_) {
    if (s.busy) continue;
    s.len = build(UnitId::Outdoor, MsgType::DoorRequest, nullptr, 0, s.frame, &s.seq);
    if (!s.len) return false;
    s.busy = true;
    s.kind = SlotKind::DoorRequest;
    s.peer = UnitId::Outdoor;
    s.retries = 0;
    s.next_at = now_ms + kAckTimeoutMs;
    if (h_) h_->link_send(s.peer, s.frame, s.len);
    stats_.tx_frames++;
    return true;
  }
  stats_.door_failed++;
  return false;
}

bool Link::send_ring(UnitId peer, uint32_t now_ms) {
  if (!network_ || peer == self_ || (int)peer > 3 || !peers_[(int)peer].has_key) {
    stats_.ring_failed++;
    return false;
  }
  for (Slot &s : slots_) {
    if (s.busy) continue;
    s.len = build(peer, MsgType::Ring, nullptr, 0, s.frame, &s.seq);
    if (!s.len) return false;
    s.busy = true;
    s.kind = SlotKind::Ring;
    s.peer = peer;
    s.retries = 0;
    s.next_at = now_ms + kAckTimeoutMs;
    if (h_) h_->link_send(peer, s.frame, s.len);
    stats_.tx_frames++;
    return true;
  }
  stats_.ring_no_slot++;
  return false;
}

bool Link::send_event(UnitId peer, EventCode code, const uint8_t *data, size_t len, uint32_t now_ms) {
  if (!network_ || peer == self_ || (int)peer > 3 || !peers_[(int)peer].has_key || len + 1 > kMaxPayload) {
    stats_.events_failed++;
    return false;
  }
  uint8_t payload[kMaxPayload];
  payload[0] = (uint8_t)code;
  if (len) memcpy(payload + 1, data, len);
  for (Slot &s : slots_) {
    if (s.busy) continue;
    s.len = build(peer, MsgType::Event, payload, len + 1, s.frame, &s.seq);
    if (!s.len) return false;
    s.busy = true;
    s.kind = SlotKind::Event;
    s.code = code;
    s.peer = peer;
    s.retries = 0;
    s.next_at = now_ms + kAckTimeoutMs;
    if (h_) h_->link_send(peer, s.frame, s.len);
    stats_.tx_frames++;
    return true;
  }
  stats_.events_failed++;
  return false;
}

void Link::tick(uint32_t now_ms) {
  for (Slot &s : slots_) {
    if (!s.busy || !due(now_ms, s.next_at)) continue;
    if (s.retries < kMaxRetries) {
      s.retries++;
      s.next_at += kAckTimeoutMs;
      if (network_ && h_) {
        h_->link_send(s.peer, s.frame, s.len);  // identical frame: same seq, so the receiver can dedupe
        stats_.tx_frames++;
      }
      stats_.tx_retries++;
    } else {
      s.busy = false;
      if (s.kind == SlotKind::Ring) {
        stats_.ring_failed++;
        if (h_) h_->link_ring_result(s.peer, false);
      } else if (s.kind == SlotKind::Event) {
        stats_.events_failed++;
        if (h_) h_->link_event_result(s.peer, s.code, false);
      } else {
        stats_.door_failed++;
        if (h_) h_->link_door_result(DoorResult::NotAllowed, false);
      }
    }
  }

  if (!network_) return;
  for (int i = 1; i < 4; i++) {
    Peer &p = peers_[i];
    if (!p.has_key || !due(now_ms, p.next_ping)) continue;
    if (p.ping_outstanding && ++p.missed >= kMissedForUnreachable && p.reachable) {
      p.reachable = false;
      p.ever_answered = false;  // search faster again
      if (h_) h_->link_peer_state((UnitId)i, false);
    }
    send_ping((UnitId)i, now_ms);
    p.ping_outstanding = true;
    p.next_ping = now_ms + (p.ever_answered ? kHeartbeatMs : kHeartbeatSearchMs);
  }
}

}  // namespace cb
