// cb_remote_logic.h - remote opening by phone (Softwarearchitektur 4.5, decision R27), hardware-free, PC-tested.
//
// The use case: a child stands locked out at the door, no adult is home. Flow (R27, 12.11.2026):
//   1. ring at EG or OG -> the outdoor unit draws one token per phone of THAT flat (128 bit, valid kRemoteTokenMs)
//      and sends each phone a push via ntfy.sh to the phone's own push topic. The push carries an ntfy "http"
//      action: one tap on "Tür öffnen" in the notification POSTs "CBR2 <token>" to the phone's own command topic.
//      Only the ntfy app is needed - no second app, no script on the phone.
//   2. the outdoor unit reads the command topics of all phones in one stream, finds the phone by the topic,
//      checks scheme and token (issued for exactly this phone, unused, fresh), and only then asks the door task to
//      open; after an opening all tokens of that flat are gone
//
// Why this is acceptable without a phone signature (R20 had one, R27 replaces it): since port step 10c the topics
// are not guessable any more - they are derived from a 256-bit key that lives only in the phone's setup link, the
// indoor unit and the outdoor unit. Whoever does not know a phone's topics can neither read its pushes nor send it
// commands. A token is good for one opening, for one phone, for kRemoteTokenMs and only after a real ring; every
// opening is confirmed by push and logged. The operator of ntfy.sh sees topics and tokens and could open within
// 5 min after a ring - that residual risk was accepted on purpose (two apps and visible script code looked like a
// hack to the household). Removing a phone drops its topics at once.
//
// Topics: "cb" + 32 hex characters (128 bit) of HMAC-SHA256(key = phone key as text, "CBT1|push" / "CBT1|cmd").
#pragma once

#include <cstddef>
#include <cstdint>

namespace cb {

constexpr uint32_t kRemoteTokenMs = 5 * 60 * 1000;  // SA 4.5: 5 min
constexpr int kRemoteRings = 3;                     // the newest 3 rings stay usable (visitor rings twice)
constexpr int kPhonesPerFlat = 4;
constexpr int kRemotePhones = 2 * kPhonesPerFlat;   // EG + OG
constexpr int kRemoteTokens = kRemoteRings * kPhonesPerFlat;
constexpr size_t kPhoneIdMax = 8;                   // phone id: 1..8 of [A-Za-z0-9-]
constexpr size_t kPhoneKeyHex = 64;
constexpr size_t kTopicLen = 34;                    // "cb" + 32 hex

enum class RemoteResult : uint8_t {
  Ok,
  Malformed,     // not "<scheme> <token>" with the right lengths and characters
  UnknownScheme,
  UnknownPhone,  // message on a topic that belongs to no phone (removed phone, or a stray message)
  UnknownToken,  // never issued for this phone, already used, or from before a reboot
  Expired,
};
const char *remote_result_name(RemoteResult r);

// The two topics of a phone, derived from its key (64 lower-case hex characters). False for an invalid key.
bool phone_topics(const char *key_hex, char push[kTopicLen + 1], char cmd[kTopicLen + 1]);
bool valid_phone_id(const char *id);

class RemoteAuth {
 public:
  // flat: the UnitId value of EG (2) or OG (3). Same id in the same flat = replaced (new phone of the same person).
  bool set_phone(int slot, const char *id, const char *key_hex, uint8_t flat);
  int add_phone(const char *id, const char *key_hex, uint8_t flat);  // -> slot, -1 if invalid or the flat is full
  bool remove_phone(const char *id, uint8_t flat);
  int phone_count() const;
  int phone_count(uint8_t flat) const;
  bool used(int slot) const { return slot >= 0 && slot < kRemotePhones && phones_[slot].used; }
  const char *phone_id(int slot) const { return phones_[slot].id; }
  uint8_t phone_flat(int slot) const { return phones_[slot].flat; }
  const char *push_topic(int slot) const { return phones_[slot].push; }
  const char *cmd_topic(int slot) const { return phones_[slot].cmd; }
  // Comma-separated command topics of all phones (for one ntfy stream); false if it does not fit.
  bool cmd_topics(char *out, size_t cap) const;

  // New ring at `flat`: one token per phone of the flat (hardware RNG on the device). Fills slots/tokens,
  // returns how many. The tokens of the oldest ring of that flat make room.
  int issue(uint8_t flat, uint32_t now_ms, int slots[kPhonesPerFlat], char tokens[kPhonesPerFlat][33]);

  // Checks a command that arrived on `topic`. Does NOT use up the token - call consume_flat() once the door really
  // opened, so a refusal (lock-out time) leaves it usable for a second try. slot_out: the phone (or -1).
  RemoteResult check(const char *topic, const char *msg, uint32_t now_ms, int *slot_out) const;
  void consume_flat(uint8_t flat);
  int active(uint32_t now_ms) const;

 private:
  struct Token {
    char hex[33];
    uint32_t issued;
    uint32_t ring;  // ring number: the oldest ring of a flat is replaced as a whole
    int8_t slot;    // phone the token belongs to
    bool used;
  } tokens_[kRemoteTokens * 2] = {};
  uint32_t rings_ = 0;
  struct Phone {
    char id[kPhoneIdMax + 1];
    char key[kPhoneKeyHex + 1];
    char push[kTopicLen + 1];
    char cmd[kTopicLen + 1];
    uint8_t flat;
    bool used;
  } phones_[kRemotePhones] = {};
};

// Reads the string value of "key" from one JSON object (one line of the ntfy stream). Handles escapes
// (\" \\ \/ \n \t \uXXXX as '?'), returns false if the key is missing, not a string, or does not fit.
// Deliberately tiny: the ntfy lines are flat, and a parser that cannot fit a value refuses it.
bool json_get_string(const char *json, const char *key, char *out, size_t cap);

}  // namespace cb
