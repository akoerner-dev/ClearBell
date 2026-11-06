// cb_protocol.h - ClearBell protocol v2: frame layout, message types, encode and verify.
//
// Every UDP datagram between the units is one frame (Softwarearchitektur 4.2):
//
//   offset size field     meaning
//   0      2    magic     'C','B' - drops foreign packets without any crypto work
//   2      1    version   2
//   3      1    type      MsgType below
//   4      1    sender    UnitId of the sending unit
//   5      1    receiver  UnitId of the intended receiver - a frame for EG is invalid at OG
//   6      4    session   random number chosen by the sender at every boot (little endian)
//   10     4    seq       running number within the session (little endian)
//   14     n    payload   0..kMaxPayload bytes, meaning depends on type
//   14+n   16   tag       HMAC-SHA256(link key, "CBv2" || bytes 0..14+n-1), first 16 bytes
//
// The payload length is not transmitted: it follows from the datagram length (UDP keeps boundaries).
// This file only knows bytes. Who may talk to whom, sessions, duplicates and retries live in cb_link.
#pragma once

#include <cstddef>
#include <cstdint>

#include "cb_crypto.h"

namespace cb {

constexpr uint8_t kMagic0 = 'C';
constexpr uint8_t kMagic1 = 'B';
constexpr uint8_t kVersion = 2;
constexpr size_t kHeaderLen = 14;
constexpr size_t kTagLen = 16;  // HMAC truncated to 128 bit (reasoning in Softwarearchitektur 4.2)
constexpr size_t kMaxPayload = 48;
constexpr size_t kMinFrame = kHeaderLen + kTagLen;
constexpr size_t kMaxFrame = kHeaderLen + kMaxPayload + kTagLen;
constexpr size_t kNonceLen = 16;  // challenges for session setup and door command

// The three units of one installation. The numeric values are on the wire - never renumber.
// EG/OG are the names of the two flats (ground floor / upper floor) used throughout the schematics.
enum class UnitId : uint8_t {
  None = 0,
  Outdoor = 1,  // Außeneinheit at the front door
  EG = 2,       // indoor unit, ground-floor flat
  OG = 3,       // indoor unit, upper-floor flat
};

// Message types (Softwarearchitektur 4.2, table "Nachrichtentypen"). Values are on the wire.
enum class MsgType : uint8_t {
  Ping = 1,           // both ways; payload: nonce[16] - "prove you are alive in your current session"
  Pong = 2,           // answer to Ping; payload: the nonce of the Ping
  Ring = 3,           // outdoor -> indoor; payload: none
  RingAck = 4,        // indoor -> outdoor; payload: session (u32) + seq (u32) of the Ring
  DoorRequest = 5,    // indoor -> outdoor; payload: none
  DoorChallenge = 6,  // outdoor -> indoor; payload: session + seq of the request, nonce[16]
  DoorConfirm = 7,    // indoor -> outdoor; payload: nonce[16] of the challenge
  DoorAck = 8,        // outdoor -> indoor; payload: session + seq of the confirm, DoorResult (u8)
  Event = 9,          // outdoor <-> indoor; payload: event code (u8) + data (0..47 B), delivered like a Ring
  EventAck = 10,      // answer to Event; payload: session + seq of the Event
};

// Event codes (MsgType::Event, port step 10b/10c). Delivery, duplicate suppression and replay protection are the
// same as for a Ring; what an event may do is decided by the application (main.cpp / cb_pairing).
enum class EventCode : uint8_t {
  MaintenanceRequest = 1,  // indoor -> outdoor: someone at the indoor unit (button = presence) opens the
                           // outdoor unit's maintenance for 10 min - no need to open the outdoor housing
  PhoneAdd = 2,            // indoor -> outdoor: phone id + phone key, encrypted with the link key (10c)
  PhoneRemove = 3,         // indoor -> outdoor: phone id (10c)
};
// Every answer names the session AND seq of the message it answers. That ties it to one message of one
// boot of the asking unit, so a recorded answer from an earlier boot (where seq numbers repeat) can never
// be taken for a new one (test: link_old_ack_cannot_confirm_new_ring).

// Result carried by DoorAck.
enum class DoorResult : uint8_t {
  Opened = 0,      // accepted, the door task pulses the opener
  Locked = 1,      // accepted but refused: lock-out time after the previous opening still running
  BadNonce = 2,    // challenge unknown, expired (2 s) or already used
  NotAllowed = 3,  // this unit may not open the door right now (e.g. maintenance mode)
};

// A decoded frame. `payload` points INTO the buffer passed to verify_frame() - valid only as long as
// that buffer lives.
struct Frame {
  MsgType type;
  UnitId sender;
  UnitId receiver;
  uint32_t session;
  uint32_t seq;
  const uint8_t *payload;
  size_t payload_len;
};

// Why a received datagram was rejected. Counted per reason on the status page; a rejected frame is
// never answered (Softwarearchitektur 4.2, "was scheitert, wird verworfen, ohne es zu beantworten").
enum class RxError : uint8_t {
  Ok = 0,
  BadLength,    // shorter than header + tag, or longer than kMaxFrame
  BadMagic,     // not ClearBell at all
  BadVersion,   // ClearBell, but a version this firmware does not speak
  BadAddress,   // receiver is not this unit, or sender is not a unit we share a key with
  BadTag,       // authentication failed - wrong key or modified in transit
};

// Builds a complete frame into `out` (at least kMaxFrame bytes). Returns the frame length, or 0 if the
// payload is too long. `key` is the link key between sender and receiver (kKeyLen bytes).
size_t encode_frame(const Frame &f, const uint8_t *key, uint8_t *out, size_t out_size);

// Cheap pre-checks that need no key: length, magic, version, plausible addresses. Returns the (not yet
// authenticated!) sender and receiver so the caller can rate-limit and pick the link key before spending
// time on the HMAC.
RxError precheck_frame(const uint8_t *buf, size_t len, UnitId *sender, UnitId *receiver);

// Full verification with the link key, in the order of Softwarearchitektur 4.2: precheck, then the tag
// in constant time. Fills `f` only on Ok. Sessions and sequence numbers are checked by cb_link.
RxError verify_frame(const uint8_t *buf, size_t len, const uint8_t *key, Frame *f);

const char *msg_type_name(MsgType t);
const char *unit_name(UnitId u);
const char *rx_error_name(RxError e);

}  // namespace cb
