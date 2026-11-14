// cb_protocol.cpp - encode and verify protocol v2 frames. Layout: see cb_protocol.h.
#include "cb_protocol.h"

#include "cb_labels.h"

#include <cstring>

#include "cb_bytes.h"

namespace cb {

namespace {

// Domain separation: the tag covers "CBv2" before the frame bytes, so a tag computed with the same key
// for any other purpose (a future protocol version, the phone signature) can never be valid here.
const uint8_t *const kDomain = reinterpret_cast<const uint8_t *>(kLabelFrame);  // "CBv2", list in cb_labels.h
constexpr size_t kDomainLen = 4;

bool compute_tag(const uint8_t *key, const uint8_t *frame, size_t body_len, uint8_t tag[kTagLen]) {
  uint8_t mac[kMacLen];
  Slice parts[2] = {{kDomain, kDomainLen}, {frame, body_len}};
  if (!hmac_sha256(key, kKeyLen, parts, 2, mac)) return false;
  memcpy(tag, mac, kTagLen);
  return true;
}

bool is_unit(uint8_t v) {
  return v >= (uint8_t)UnitId::Outdoor && v <= (uint8_t)UnitId::OG;
}

}  // namespace

size_t encode_frame(const Frame &f, const uint8_t *key, uint8_t *out, size_t out_size) {
  size_t total = kHeaderLen + f.payload_len + kTagLen;
  if (f.payload_len > kMaxPayload || out_size < total) return 0;
  out[0] = kMagic0;
  out[1] = kMagic1;
  out[2] = kVersion;
  out[3] = (uint8_t)f.type;
  out[4] = (uint8_t)f.sender;
  out[5] = (uint8_t)f.receiver;
  put_u32le(out + 6, f.session);
  put_u32le(out + 10, f.seq);
  if (f.payload_len) memcpy(out + kHeaderLen, f.payload, f.payload_len);
  if (!compute_tag(key, out, kHeaderLen + f.payload_len, out + kHeaderLen + f.payload_len)) return 0;
  return total;
}

RxError precheck_frame(const uint8_t *buf, size_t len, UnitId *sender, UnitId *receiver) {
  if (len < kMinFrame || len > kMaxFrame) return RxError::BadLength;
  if (buf[0] != kMagic0 || buf[1] != kMagic1) return RxError::BadMagic;
  if (buf[2] != kVersion) return RxError::BadVersion;
  if (!is_unit(buf[4]) || !is_unit(buf[5]) || buf[4] == buf[5]) return RxError::BadAddress;
  *sender = (UnitId)buf[4];
  *receiver = (UnitId)buf[5];
  return RxError::Ok;
}

RxError verify_frame(const uint8_t *buf, size_t len, const uint8_t *key, Frame *f) {
  UnitId sender, receiver;
  RxError e = precheck_frame(buf, len, &sender, &receiver);
  if (e != RxError::Ok) return e;
  size_t body_len = len - kTagLen;
  uint8_t expected[kTagLen];
  // Fail closed: if the HMAC cannot be computed, the frame counts as forged.
  if (!compute_tag(key, buf, body_len, expected)) return RxError::BadTag;
  if (!ct_equal(expected, buf + body_len, kTagLen)) return RxError::BadTag;
  f->type = (MsgType)buf[3];
  f->sender = sender;
  f->receiver = receiver;
  f->session = get_u32le(buf + 6);
  f->seq = get_u32le(buf + 10);
  f->payload = buf + kHeaderLen;
  f->payload_len = body_len - kHeaderLen;
  return RxError::Ok;
}

const char *msg_type_name(MsgType t) {
  switch (t) {
    case MsgType::Ping: return "PING";
    case MsgType::Pong: return "PONG";
    case MsgType::Ring: return "RING";
    case MsgType::RingAck: return "RING_ACK";
    case MsgType::DoorRequest: return "DOOR_REQUEST";
    case MsgType::DoorChallenge: return "DOOR_CHALLENGE";
    case MsgType::DoorConfirm: return "DOOR_CONFIRM";
    case MsgType::DoorAck: return "DOOR_ACK";
    case MsgType::Event: return "EVENT";
    case MsgType::EventAck: return "EVENT_ACK";
  }
  return "?";
}

const char *unit_name(UnitId u) {
  switch (u) {
    case UnitId::Outdoor: return "AUSSEN";
    case UnitId::EG: return "EG";
    case UnitId::OG: return "OG";
    case UnitId::None: break;
  }
  return "-";
}

const char *rx_error_name(RxError e) {
  switch (e) {
    case RxError::Ok: return "ok";
    case RxError::BadLength: return "length";
    case RxError::BadMagic: return "magic";
    case RxError::BadVersion: return "version";
    case RxError::BadAddress: return "address";
    case RxError::BadTag: return "tag";
  }
  return "?";
}

}  // namespace cb
