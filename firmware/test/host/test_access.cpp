// test_access.cpp - VK-08 part 2 / RV-45: who may use which web endpoint in which mode (cb_access.h). The table
// describes the behaviour as of 14.11.2026 UNCHANGED; Phase 2 of the security plan (decisions E2/E3) changes it here
// first, so every rule change shows up as a test change.
#include <initializer_list>

#include "cb_access.h"
#include "mini_test.h"

using namespace cb;

namespace {

AccessInput in(Mode m, bool ap = false) {
  AccessInput i;
  i.mode = m;
  i.from_ap = ap;
  i.provisioned = true;
  i.flat_set = true;
  i.indoor_paired = true;
  i.target_paired = false;
  return i;
}

bool ok(Endpoint e, const AccessInput &i) { return mode_access(e, i).ok; }

const Mode kModes[] = {Mode::Normal, Mode::Maintenance, Mode::Setup};

}  // namespace

TEST(access_every_writing_endpoint_needs_origin_and_login) {
  // Writing = POST/PUT. All of them go through authorize() (cross-site check, then login or setup access point) -
  // except the indoor unit's /pair, which proves the device code itself and is called unit-to-unit.
  for (int e = 0; e < (int)Endpoint::Count; e++) {
    EndpointInfo info = endpoint_info((Endpoint)e);
    CHECK(info.path != nullptr);
    if ((Endpoint)e == Endpoint::PairAccept) {
      CHECK(info.writes && !info.needs_login && !info.origin_checked);
      continue;
    }
    CHECK(info.needs_login);
    if (info.writes) CHECK(info.origin_checked);
  }
}

TEST(access_reading_and_harmless_endpoints_work_in_every_mode) {
  for (Endpoint e : {Endpoint::Page, Endpoint::StatusJson, Endpoint::PasswordNonce, Endpoint::SetupInfo,
                     Endpoint::SetupScan, Endpoint::PairList, Endpoint::PhoneTest, Endpoint::SettingsVolume,
                     Endpoint::SettingsTone})
    for (Mode m : kModes) CHECK(ok(e, in(m)));
}

TEST(access_maintenance_only_endpoints) {
  for (Endpoint e : {Endpoint::ToneUpload, Endpoint::Update, Endpoint::SettingsLed, Endpoint::SettingsOwnerKey,
                     Endpoint::PasswordChange, Endpoint::OutdoorMaint}) {
    CHECK(!ok(e, in(Mode::Normal)));
    CHECK(ok(e, in(Mode::Maintenance)));
    CHECK(!ok(e, in(Mode::Setup)));
    CHECK(!ok(e, in(Mode::Normal, true)));  // the access point does not replace maintenance here
  }
  CHECK(mode_access(Endpoint::Update, in(Mode::Normal)).status[0] == '4');
}

TEST(access_setup_page_changes_only_in_setup_or_over_the_access_point) {
  for (Endpoint e : {Endpoint::SetupSave, Endpoint::SetupReset}) {
    CHECK(!ok(e, in(Mode::Normal)));
    CHECK(!ok(e, in(Mode::Maintenance)));
    CHECK(ok(e, in(Mode::Setup)));
    CHECK(ok(e, in(Mode::Normal, true)));  // RV-04: today the access point allows it in ANY mode (Phase 2, E2)
  }
}

TEST(access_flat_name_and_outdoor_maintenance_rules) {
  CHECK(!ok(Endpoint::FlatName, in(Mode::Normal)));
  CHECK(ok(Endpoint::FlatName, in(Mode::Maintenance)));
  CHECK(ok(Endpoint::FlatName, in(Mode::Setup)));
  CHECK(ok(Endpoint::FlatName, in(Mode::Normal, true)));
  AccessInput i = in(Mode::Maintenance);
  i.flat_set = false;
  CHECK(!ok(Endpoint::FlatName, i));
  i = in(Mode::Maintenance);
  i.indoor_paired = false;
  Access a = mode_access(Endpoint::OutdoorMaint, i);
  CHECK(!a.ok);
  CHECK(a.status[0] == '4' && a.status[1] == '0' && a.status[2] == '9');
}

TEST(access_pairing_rules_on_the_outdoor_unit) {
  for (Mode m : kModes) CHECK(ok(Endpoint::PairStart, in(m)));  // RV-08: a FREE flat pairs even in normal mode
  AccessInput i = in(Mode::Normal);
  i.target_paired = true;
  CHECK(!ok(Endpoint::PairStart, i));  // replacing needs maintenance or setup
  i.mode = Mode::Maintenance;
  CHECK(ok(Endpoint::PairStart, i));
  for (Endpoint e : {Endpoint::PairRemove, Endpoint::DoorPulse}) {
    CHECK(!ok(e, in(Mode::Normal)));
    CHECK(ok(e, in(Mode::Maintenance)));
    CHECK(ok(e, in(Mode::Setup)));
  }
}

TEST(access_pairing_offer_on_the_indoor_unit) {
  AccessInput i = in(Mode::Normal);
  CHECK(!ok(Endpoint::PairAccept, i));  // paired + normal mode: refused
  i.indoor_paired = false;
  CHECK(ok(Endpoint::PairAccept, i));
  i = in(Mode::Maintenance);
  CHECK(ok(Endpoint::PairAccept, i));  // re-pairing with the button in the flat
  i.provisioned = false;
  CHECK(!ok(Endpoint::PairAccept, i));  // not set up (WiFi, key) yet
}

TEST(access_phone_management_only_in_maintenance_of_a_paired_flat) {
  for (Endpoint e : {Endpoint::PhoneAdd, Endpoint::PhoneRemove, Endpoint::PhoneLink}) {
    CHECK(!ok(e, in(Mode::Normal)));
    CHECK(ok(e, in(Mode::Maintenance)));
    CHECK(!ok(e, in(Mode::Setup)));
    AccessInput i = in(Mode::Maintenance);
    i.flat_set = false;
    CHECK(!ok(e, i));
    i = in(Mode::Maintenance);
    i.indoor_paired = false;
    CHECK(!ok(e, i));
  }
  CHECK(ok(Endpoint::PhoneTest, in(Mode::Normal)));  // a test push only reaches the phone itself - no precondition
}
