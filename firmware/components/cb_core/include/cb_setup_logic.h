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

// ---- Automatic access point (R30, E2_Zugangspunkt, VK-02) ---------------------------------------------------------
// A set-up unit that has lost its WiFi opens the setup access point by itself after kAutoApAfterMs, so the buyer can
// enter a new WiFi without the button. It stays open for at most kAutoApForMs per WiFi loss (a jammer must not keep
// it open for ever); afterwards only the WiFi coming back once, a reboot or the button (setup mode) opens it again.
// Over this access point only the WiFi may be changed (cb_access: SetupSave yes, SetupRole/SetupReset no).
constexpr uint32_t kAutoApAfterMs = 5UL * 60 * 1000;
constexpr uint32_t kAutoApForMs = 30UL * 60 * 1000;
// lost_ms: how long the WiFi has been missing without a break.
bool auto_ap_due(uint32_t lost_ms);

// RV-16: did a request really come over the setup access point? All addresses as lwIP stores them (s_addr, network
// byte order - only compared under masks, so the byte order does not matter). True only if the local address is the
// access point's, the peer lies in the access point's net (a home-network device cannot fake such a source: the
// handshake would be answered over the access point), and the home network (sta_ip/sta_mask, 0 = not connected) does
// not overlap the access point's net - then the source would be ambiguous.
bool ap_request(uint32_t local, uint32_t peer, uint32_t ap_ip, uint32_t ap_mask, uint32_t sta_ip, uint32_t sta_mask);

// ---- PMF (R30, RV-09): protected management frames "required" where the router can do it ------------------------
// ESP-IDF always uses PMF when the router offers it (pmf_cfg.capable is obsolete) - forged disconnect frames are then
// ignored. "required" additionally refuses a router WITHOUT PMF, i.e. a fake router of the same name that offers
// none. Whether the home router can do PMF is learned once per WiFi (trust on first use) and stored; entering a WiFi
// on the setup page forgets it.
//   Unknown  try with "required"; reason 210 (no AP with compatible security) -> No, connected -> Yes
//   Yes      always "required" - a later 210 is a downgrade (or a new router: then enter the WiFi again)
//   No       PMF optional (old router); the status page says "PMF nicht aktiv"
enum class PmfLearned : uint8_t { Unknown = 0, Yes = 1, No = 2 };
constexpr uint8_t kReasonNoCompatibleSecurity = 210;  // WIFI_REASON_NO_AP_FOUND_W_COMPATIBLE_SECURITY
inline bool pmf_required(PmfLearned l) { return l != PmfLearned::No; }
// State after one connection attempt that was made with pmf_required(l).
PmfLearned pmf_after_attempt(PmfLearned l, bool connected, uint8_t reason);

// ---- WiFi trial on the setup page (SW-Entscheid_Adresse_in_Erfolgsmeldung, 22.11.2026) ---------------------------
// Over the access point the entered WiFi is TRIED first (access point and station at the same time); only a working
// one is stored. The page then shows the unit's real address as a link. The buyer wants the two failures told apart:
// wrong password -> "Falsche Anmeldedaten", form stays; network not found -> its own message.
enum class WifiTrial : uint8_t { Running, Connected, WrongPassword, NotFound, Failed };
constexpr uint32_t kWifiTrialMs = 25000;         // no IP within this time -> Failed
constexpr uint32_t kWifiTrialRebootMs = 45000;   // after success: time to read the address, then the restart
// Verdict for a disconnect reason (ESP-IDF wifi_err_reason_t) during the trial.
WifiTrial wifi_trial_verdict(uint8_t reason);

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
