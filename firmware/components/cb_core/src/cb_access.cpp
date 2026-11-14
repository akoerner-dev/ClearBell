// cb_access.cpp - see cb_access.h. The texts are the ones the handlers used before (no visible change).
#include "cb_access.h"

namespace cb {

namespace {
constexpr Access kOk{true, "200 OK", ""};
constexpr const char *k403 = "403 Forbidden";
constexpr const char *k409 = "409 Conflict";

Access deny(const char *status, const char *msg) { return Access{false, status, msg}; }
bool maint(const AccessInput &in) { return in.mode == Mode::Maintenance; }
}  // namespace

EndpointInfo endpoint_info(Endpoint e) {
  switch (e) {
    case Endpoint::Page: return {"/", false, true, true};
    case Endpoint::StatusJson: return {"/status.json", false, true, true};
    case Endpoint::ToneUpload: return {"/ton/*", true, true, true};
    case Endpoint::Update: return {"/update", true, true, true};
    case Endpoint::SettingsVolume: return {"/settings vol", true, true, true};
    case Endpoint::SettingsTone: return {"/settings tone", true, true, true};
    case Endpoint::SettingsLed: return {"/settings led", true, true, true};
    case Endpoint::SettingsOwnerKey: return {"/settings ownerkey|owneronly", true, true, true};
    case Endpoint::PasswordNonce: return {"/settings/pw-nonce", false, true, true};
    case Endpoint::PasswordChange: return {"/settings/password", true, true, true};
    case Endpoint::SetupInfo: return {"/setup/info", false, true, true};
    case Endpoint::SetupScan: return {"/setup/scan", false, true, true};
    case Endpoint::SetupSave: return {"/setup/save", true, true, true};
    case Endpoint::SetupReset: return {"/setup/reset", true, true, true};
    case Endpoint::PairList: return {"/pair/list", false, true, true};
    case Endpoint::PairStart: return {"/pair/start", true, true, true};
    case Endpoint::PairRemove: return {"/pair/remove", true, true, true};
    case Endpoint::DoorPulse: return {"/door/pulse", true, true, true};
    case Endpoint::PairAccept: return {"/pair", true, false, false};  // the offer proves the device code
    case Endpoint::OutdoorMaint: return {"/outdoor-maint", true, true, true};
    case Endpoint::FlatName: return {"/flat/name", true, true, true};
    case Endpoint::PhoneAdd: return {"/phone/add", true, true, true};
    case Endpoint::PhoneRemove: return {"/phone/remove", true, true, true};
    case Endpoint::PhoneLink: return {"/phone/link", true, true, true};
    case Endpoint::PhoneTest: return {"/phone/test", true, true, true};
    case Endpoint::Count: break;
  }
  return {nullptr, false, false, false};
}

Access mode_access(Endpoint e, const AccessInput &in) {
  switch (e) {
    case Endpoint::Page:
    case Endpoint::StatusJson:
    case Endpoint::PasswordNonce:
    case Endpoint::SetupInfo:
    case Endpoint::SetupScan:
    case Endpoint::PairList:
    case Endpoint::PhoneTest:
    case Endpoint::SettingsVolume:
    case Endpoint::SettingsTone:
      return kOk;
    case Endpoint::ToneUpload:
    case Endpoint::Update:
      return maint(in) ? kOk : deny(k403, "nur in der Wartung (Boot-Taster 5 s halten)");
    case Endpoint::SettingsLed:
      return maint(in) ? kOk : deny(k403, "LED-Abgleich nur in der Wartung (Boot-Taster 5 s halten)");
    case Endpoint::SettingsOwnerKey:
      return maint(in) ? kOk : deny(k403, "Update-Schlüssel nur in der Wartung (Boot-Taster 5 s halten)");
    case Endpoint::PasswordChange:
      return maint(in) ? kOk : deny(k403, "Passwort ändern nur in der Wartung (Boot-Taster 5 s halten)");
    case Endpoint::SetupSave:
    case Endpoint::SetupReset:
      // RV-04: a client of the access point counts as present in ANY mode - security plan Phase 2 (E2) changes it
      return in.from_ap || in.mode == Mode::Setup ? kOk : deny(k403, "nur in der Einrichtung (Boot-Taster 2 × 5 s)");
    case Endpoint::PairStart:
      // RV-08: a free flat pairs in normal mode too - security plan Phase 2 (E3) changes it
      if (in.target_paired && in.mode == Mode::Normal)
        return deny(k403, "Diese Wohnung ist schon gekoppelt - zum Ersetzen erst die Wartung der Außeneinheit öffnen");
      return kOk;
    case Endpoint::PairRemove:
    case Endpoint::DoorPulse:
      return in.mode != Mode::Normal ? kOk : deny(k403, "nur in der Wartung");
    case Endpoint::PairAccept:
      if (in.indoor_paired && in.mode == Mode::Normal)
        return deny(k409, "Inneneinheit ist schon gekoppelt - zum Neu-Koppeln dort Wartung öffnen (Boot-Taster 5 s)");
      if (!in.provisioned) return deny(k409, "Inneneinheit ist noch nicht eingerichtet (WLAN, Wohnung)");
      return kOk;
    case Endpoint::OutdoorMaint:
      if (!maint(in)) return deny(k403, "nur in der Wartung dieser Inneneinheit (Boot-Taster 5 s)");
      if (!in.indoor_paired) return deny(k409, "nicht gekoppelt");
      return kOk;
    case Endpoint::FlatName:
      if (in.mode == Mode::Normal && !in.from_ap)
        return deny(k403, "nur in der Wartung dieser Inneneinheit (Boot-Taster 5 s)");
      if (!in.flat_set) return deny(k409, "erst die Klingeltaste wählen (Einrichtung)");
      return kOk;
    case Endpoint::PhoneAdd:
    case Endpoint::PhoneRemove:
    case Endpoint::PhoneLink:
      if (!in.flat_set) return deny(k403, "Wohnung nicht eingerichtet");
      if (!in.indoor_paired) return deny(k403, "erst mit der Außeneinheit koppeln");
      if (!maint(in)) return deny(k403, "nur in der Wartung (Boot-Taster 5 s)");
      return kOk;
    case Endpoint::Count:
      break;
  }
  return deny(k403, "unbekannt");
}

}  // namespace cb
