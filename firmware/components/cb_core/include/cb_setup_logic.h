// cb_setup_logic.h - helpers of the setup without PC (port step 10a), hardware-free, PC-tested.
//
//   device code   16 characters of Crockford Base32 (0-9, A-Z without I L O U), ~80 bit, printed on the label in
//                 groups of four ("K7QF-M2XA-9TRD-H4WE"). Typing is forgiving: lower case, dashes, spaces and the
//                 look-alikes O->0, I/L->1 are accepted. Generated at the factory (tools/cb.py werk) - the same
//                 rules exist there in Python.
//   form fields   the setup page posts "application/x-www-form-urlencoded"; form_get() decodes %XX and '+'.
//   WiFi QR       "WIFI:T:WPA;S:<ssid>;P:<code>;;" with the escaping phones expect (\ ; , : ").
#pragma once

#include <cstddef>
#include <cstdint>

namespace cb {

constexpr size_t kDeviceCodeLen = 16;

// Normalises a typed code into 16 canonical characters (out[17]). False if it cannot be a device code.
bool device_code_normalize(const char *in, char out[kDeviceCodeLen + 1]);
// "K7QFM2XA9TRDH4WE" -> "K7QF-M2XA-9TRD-H4WE" (out[20]).
void device_code_group(const char *code, char out[20]);
// 16 canonical characters from 10 random bytes (80 bit).
void device_code_from_bytes(const uint8_t rnd[10], char out[kDeviceCodeLen + 1]);

// Value of `key` in a urlencoded body; decoded. False if missing, malformed or longer than cap-1.
bool form_get(const char *body, const char *key, char *out, size_t cap);

// The text of a WiFi-join QR code for the setup access point.
bool wifi_qr_text(const char *ssid, const char *password, char *out, size_t cap);

// ---- Flat names (FW-B-21) --------------------------------------------------------------------------------------
// Internally the two flats stay the two bell keys of the outdoor unit: UnitId EG (2) = key at the BOTTOM, OG (3) =
// key at the TOP (the usual order on a door panel: upper flat, upper key; mounting decides, the wire numbers stay).
// The buyer gives each flat a free name ("Familie Yılmaz", "Praxis Dr. Weiß"); it is shown wherever the flat
// appears (status pages, pairing list, push "Es klingelt – <Name>"). The indoor unit owns it and sends it to the
// outdoor unit (EventCode::FlatName), so it fits into one event: at most 24 characters AND 40 bytes of UTF-8.
constexpr size_t kFlatNameMaxChars = 24;
constexpr size_t kFlatNameMaxBytes = 40;

// Trims, folds runs of white space into one space. "" is allowed (= no own name, the default is shown).
// False for invalid UTF-8, control characters, or too long. out: kFlatNameMaxBytes + 1.
bool flat_name_normalize(const char *in, char out[kFlatNameMaxBytes + 1]);
// "Taste unten" (EG, 2) / "Taste oben" (OG, 3), "" otherwise.
const char *flat_key_label(uint8_t flat);
// Shown while a flat has no own name: "Wohnung unten" / "Wohnung oben".
const char *flat_default_name(uint8_t flat);
// Percent-encodes everything but A-Z a-z 0-9 - _ . ~ (for the ntfy subscribe link). False if cap is too small.
bool url_encode(const char *in, char *out, size_t cap);

}  // namespace cb
