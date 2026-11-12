// cb_provisioning.cpp - see cb_provisioning.h.
//
// FACTORY partition "fctry", namespace "cb" (tools/cb.py werk; change both together):
//   schema    u8      2 (1 = legacy development layout, see below)
//   dev_code  string  16-character device code from the label
//   name      string  optional label for logs (development boards: "AU-1", "IN-1")
//
// USER namespace "cb_user" in the nvs partition (setup page, pairing):
//   role u8 (2 = EG, 3 = OG; outdoor needs none) · wifi_ssid / wifi_pass · key_au / key_eg / key_og blob32 ·
//   name_eg / name_og (free flat name, FW-B-21: indoor its own, outdoor as sent by the indoor units) ·
//   ser_au / ser_eg / ser_og (peer unit numbers) · ip_au / ip_eg / ip_og (optional, development) ·
//   pid0..7 / pk0..7 / pf0..7 (phones of the remote opening: id, key, flat - R27; until then 0..3 and EG only)
//   · web_pass (own status-page password). topic_out / topic_in / fern_link only came from legacy data, unused.
//
// LEGACY schema 1 (before port step 10a): fctry held role, name, wifi_ssid, wifi_pass, dev_pass, key_*, ip_*,
// topics and phones. Copied once into cb_user (flag "legacy_done" in cb_sys, which the factory reset keeps).
#include "cb_provisioning.h"

#include <Arduino.h>
#include <Preferences.h>
#include <esp_mac.h>
#include <lwip/ip4_addr.h>
#include <nvs.h>
#include <nvs_flash.h>

#include <cstring>

#include "cb_config.h"
#include "cb_log.h"
#include "cb_pairing_logic.h"

namespace {

constexpr const char *kPartition = "fctry";
constexpr const char *kFactoryNs = "cb";
constexpr const char *kUserNs = "cb_user";

CbProvisioning prov;

bool read_str(nvs_handle_t h, const char *key, char *out, size_t size) {
  size_t len = size;
  out[0] = 0;
  return nvs_get_str(h, key, out, &len) == ESP_OK && out[0] != 0;
}

const char *peer_suffix(int i) {
  return i == 1 ? "au" : i == 2 ? "eg" : "og";
}

// ---- Legacy migration: copy every user field from fctry (schema 1) into cb_user, once ------------------------
void migrate_legacy(nvs_handle_t f) {
  Preferences sys;
  sys.begin("cb_sys", false);
  bool done = sys.getBool("legacy_done", false);
  if (done) {
    sys.end();
    return;
  }
  nvs_handle_t u;
  if (nvs_open(kUserNs, NVS_READWRITE, &u) != ESP_OK) {
    sys.end();
    return;
  }
  const char *strings[] = {"wifi_ssid", "wifi_pass", "ip_au", "ip_eg", "ip_og", "topic_out", "topic_in",
                           "fern_link", "pid0", "pid1", "pid2", "pid3", "pk0", "pk1", "pk2", "pk3"};
  char buf[200];
  for (const char *k : strings)
    if (read_str(f, k, buf, sizeof buf)) nvs_set_str(u, k, buf);
  uint8_t role;
  if (nvs_get_u8(f, "role", &role) == ESP_OK && role != (uint8_t)cb::UnitId::Outdoor) nvs_set_u8(u, "role", role);
  for (int i = 1; i < 4; i++) {
    char k[8];
    snprintf(k, sizeof k, "key_%s", peer_suffix(i));
    uint8_t key[cb::kKeyLen];
    size_t len = sizeof key;
    if (nvs_get_blob(f, k, key, &len) == ESP_OK && len == sizeof key) nvs_set_blob(u, k, key, sizeof key);
  }
  nvs_commit(u);
  nvs_close(u);
  sys.putBool("legacy_done", true);
  sys.end();
  cb_log("provisioning: development data (schema 1) moved into the user configuration");
}

void load_factory() {
  uint8_t mac[6];
  esp_read_mac(mac, ESP_MAC_WIFI_STA);
  snprintf(prov.serial, sizeof prov.serial, "%02hhx%02hhx%02hhx", mac[3], mac[4], mac[5]);
  snprintf(prov.name, sizeof prov.name, "ClearBell %s", prov.serial);

  if (nvs_flash_init_partition(kPartition) != ESP_OK) return;
  nvs_handle_t h;
  if (nvs_open_from_partition(kPartition, kFactoryNs, NVS_READONLY, &h) != ESP_OK) return;
  uint8_t schema = 0;
  nvs_get_u8(h, "schema", &schema);
  char name[16];
  if (read_str(h, "name", name, sizeof name)) snprintf(prov.name, sizeof prov.name, "%s", name);
  if (schema == 2) {
    prov.factory_ok = read_str(h, "dev_code", prov.dev_code, sizeof prov.dev_code);
  } else if (schema == 1) {
    prov.factory_ok = read_str(h, "dev_pass", prov.dev_code, sizeof prov.dev_code);
    migrate_legacy(h);
  }
  nvs_close(h);
}

void read_ip(nvs_handle_t h, int i) {
  char k[8], text[16];
  snprintf(k, sizeof k, "ip_%s", peer_suffix(i));
  ip4_addr_t a;
  if (read_str(h, k, text, sizeof text) && ip4addr_aton(text, &a)) prov.peer_ip[i] = a.addr;
}

const char *load_user() {
  nvs_handle_t h;
  if (nvs_open(kUserNs, NVS_READONLY, &h) != ESP_OK) return "not set up yet";
  const char *problem = nullptr;
  if (!read_str(h, "wifi_ssid", prov.wifi_ssid, sizeof prov.wifi_ssid)) problem = "no WiFi configured";
  read_str(h, "wifi_pass", prov.wifi_pass, sizeof prov.wifi_pass);  // an open network has none
#if CB_IS_OUTDOOR
  prov.role = cb::UnitId::Outdoor;
#else
  uint8_t role = 0;
  nvs_get_u8(h, "role", &role);
  prov.role = (cb::UnitId)role;
  if (!problem && prov.role != cb::UnitId::EG && prov.role != cb::UnitId::OG) problem = "flat (EG/OG) not chosen";
#endif
  for (int i = 1; i < 4; i++) {
    char k[8];
    snprintf(k, sizeof k, "key_%s", peer_suffix(i));
    size_t len = cb::kKeyLen;
    prov.has_key[i] = nvs_get_blob(h, k, prov.key[i], &len) == ESP_OK && len == cb::kKeyLen;
    snprintf(k, sizeof k, "ser_%s", peer_suffix(i));
    read_str(h, k, prov.peer_serial[i], sizeof prov.peer_serial[i]);
    read_ip(h, i);
    if (i >= 2) {
      snprintf(k, sizeof k, "name_%s", peer_suffix(i));
      read_str(h, k, prov.flat_name[i], sizeof prov.flat_name[i]);
    }
  }
  // A unit never pairs with its own kind: drop keys that do not fit the image (e.g. after a legacy migration).
#if CB_IS_OUTDOOR
  prov.has_key[(int)cb::UnitId::Outdoor] = false;
#else
  prov.has_key[(int)cb::UnitId::EG] = prov.has_key[(int)cb::UnitId::OG] = false;
#endif
  char own[33];
  snprintf(prov.dev_pass, sizeof prov.dev_pass, "%s", read_str(h, "web_pass", own, sizeof own) ? own : prov.dev_code);
  // Phones (R27). Entries from before R27 have no flat: they were EG phones (remote opening was EG only).
  for (int i = 0; i < CbProvisioning::kPhoneSlots; i++) {
    char k[8];
    snprintf(k, sizeof k, "pid%d", i);
    if (!read_str(h, k, prov.phone_id[i], sizeof prov.phone_id[0])) continue;
    snprintf(k, sizeof k, "pk%d", i);
    bool has_key = read_str(h, k, prov.phone_key[i], sizeof prov.phone_key[0]);
    snprintf(k, sizeof k, "pf%d", i);
    uint8_t flat = (uint8_t)cb::UnitId::EG;
    nvs_get_u8(h, k, &flat);
    prov.phone_flat[i] = CB_IS_OUTDOOR ? flat : (uint8_t)prov.role;
#if CB_IS_OUTDOOR
    if (!has_key) {
      prov.phone_id[i][0] = 0;  // id without key: unusable
      continue;
    }
#else
    (void)has_key;  // indoor entry from before R27: listed, but no pushes until added again
#endif
    prov.phones++;
  }
  nvs_close(h);
  return problem;
}

bool user_write(void (*fn)(nvs_handle_t, const void *), const void *arg) {
  nvs_handle_t h;
  if (nvs_open(kUserNs, NVS_READWRITE, &h) != ESP_OK) return false;
  fn(h, arg);
  bool ok = nvs_commit(h) == ESP_OK;
  nvs_close(h);
  return ok;
}

}  // namespace

void cb_provisioning_begin() {
  memset(&prov, 0, sizeof(prov));
  load_factory();
  prov.problem = load_user();
  if (!prov.factory_ok && !prov.problem) prov.problem = "no device code (factory step missing)";
  prov.valid = prov.problem == nullptr;
  if (!prov.dev_pass[0]) snprintf(prov.dev_pass, sizeof prov.dev_pass, "%s", prov.dev_code);
  if (!prov.valid) {
    memset(prov.key, 0, sizeof(prov.key));
    memset(prov.phone_key, 0, sizeof(prov.phone_key));
    cb_log("provisioning: '%s' (unit %s) NOT set up - %s", prov.name, prov.serial, prov.problem);
  } else {
    int keys = prov.has_key[1] + prov.has_key[2] + prov.has_key[3];
    cb_log("provisioning: '%s' (unit %s) role %s, WiFi '%s', %d paired peer(s)", prov.name, prov.serial,
           cb::unit_name(prov.role), prov.wifi_ssid, keys);
  }
}

const CbProvisioning &cb_prov() {
  return prov;
}

bool cb_prov_set_wifi(const char *ssid, const char *pass) {
  if (!ssid || !*ssid || strlen(ssid) > 32 || !pass || strlen(pass) > 64) return false;
  struct W {
    const char *s, *p;
  } w{ssid, pass};
  return user_write(
      [](nvs_handle_t h, const void *a) {
        const W *w = static_cast<const W *>(a);
        nvs_set_str(h, "wifi_ssid", w->s);
        nvs_set_str(h, "wifi_pass", w->p);
      },
      &w);
}

bool cb_prov_set_role(cb::UnitId role) {
#if CB_IS_OUTDOOR
  return role == cb::UnitId::Outdoor;
#else
  if (role != cb::UnitId::EG && role != cb::UnitId::OG) return false;
  uint8_t r = (uint8_t)role;
  return user_write([](nvs_handle_t h, const void *a) { nvs_set_u8(h, "role", *static_cast<const uint8_t *>(a)); },
                    &r);
#endif
}

bool cb_prov_set_flat_name(cb::UnitId flat, const char *name) {
  int i = (int)flat;
  if ((flat != cb::UnitId::EG && flat != cb::UnitId::OG) || !name || strlen(name) > cb::kFlatNameMaxBytes)
    return false;
  if (!strcmp(prov.flat_name[i], name)) return true;
  struct N {
    int i;
    const char *n;
  } a{i, name};
  bool ok = user_write(
      [](nvs_handle_t h, const void *v) {
        const N *a = static_cast<const N *>(v);
        char k[8];
        snprintf(k, sizeof k, "name_%s", peer_suffix(a->i));
        if (*a->n) nvs_set_str(h, k, a->n);
        else nvs_erase_key(h, k);
      },
      &a);
  if (ok) snprintf(prov.flat_name[i], sizeof prov.flat_name[i], "%s", name);
  return ok;
}

const char *cb_prov_flat_name(cb::UnitId flat) {
  int i = (int)flat;
  if (flat != cb::UnitId::EG && flat != cb::UnitId::OG) return cb::unit_name(flat);
  return prov.flat_name[i][0] ? prov.flat_name[i] : cb::flat_default_name((uint8_t)i);
}

bool cb_prov_set_web_pass(const char *pw) {
  if (!pw || strlen(pw) >= sizeof prov.dev_pass) return false;
  bool ok = user_write(
      [](nvs_handle_t h, const void *a) {
        const char *p = static_cast<const char *>(a);
        if (*p) nvs_set_str(h, "web_pass", p);
        else nvs_erase_key(h, "web_pass");
      },
      pw);
  if (ok) snprintf(prov.dev_pass, sizeof prov.dev_pass, "%s", *pw ? pw : prov.dev_code);
  return ok;
}

bool cb_prov_has_own_web_pass() {
  return strcmp(prov.dev_pass, prov.dev_code) != 0;
}

bool cb_prov_factory_reset() {
  bool ok = true;
  for (const char *ns : {kUserNs, "cb_set", "cb_dlog"}) {
    Preferences p;
    if (p.begin(ns, false)) {
      ok = p.clear() && ok;
      p.end();
    }
  }
  Preferences sys;  // update bookkeeping goes, the boot counter and the legacy flag stay
  if (sys.begin("cb_sys", false)) {
    sys.remove("upd_peers");
    sys.remove("upd_fault");
    sys.end();
  }
  cb_log("provisioning: FACTORY RESET %s - device code and firmware stay", ok ? "done" : "INCOMPLETE");
  return ok;
}

bool cb_prov_set_link(cb::UnitId peer, const uint8_t key[32], const char *peer_serial) {
  if ((int)peer < 1 || (int)peer > 3 || !peer_serial || strlen(peer_serial) != 6) return false;
  struct L {
    int i;
    const uint8_t *k;
    const char *s;
  } l{(int)peer, key, peer_serial};
  return user_write(
      [](nvs_handle_t h, const void *a) {
        const L *l = static_cast<const L *>(a);
        char k[8];
        snprintf(k, sizeof k, "key_%s", peer_suffix(l->i));
        nvs_set_blob(h, k, l->k, cb::kKeyLen);
        snprintf(k, sizeof k, "ser_%s", peer_suffix(l->i));
        nvs_set_str(h, k, l->s);
        snprintf(k, sizeof k, "ip_%s", peer_suffix(l->i));
        nvs_erase_key(h, k);  // a fixed development address would point at the old unit
#if CB_IS_OUTDOOR
        snprintf(k, sizeof k, "name_%s", peer_suffix(l->i));
        nvs_erase_key(h, k);  // the old unit's flat name; the new unit sends its own after the restart
#endif
      },
      &l);
}

bool cb_prov_remove_link(cb::UnitId peer) {
  if ((int)peer < 1 || (int)peer > 3) return false;
  int i = (int)peer;
  return user_write(
      [](nvs_handle_t h, const void *a) {
        int i = *static_cast<const int *>(a);
        char k[8];
        for (const char *f : {"key_%s", "ser_%s", "ip_%s", "name_%s"}) {
          snprintf(k, sizeof k, f, peer_suffix(i));
          if (CB_IS_OUTDOOR || f[0] != 'n') nvs_erase_key(h, k);  // indoor keeps its own name
        }
      },
      &i);
}

int cb_prov_phone_store(const char *id, const char *key_hex, uint8_t flat) {
  int slot = -1;
  for (int i = 0; i < CbProvisioning::kPhoneSlots; i++)
    if (prov.phone_id[i][0] && prov.phone_flat[i] == flat && !strcmp(prov.phone_id[i], id)) slot = i;
  for (int i = 0; i < CbProvisioning::kPhoneSlots && slot < 0; i++)
    if (!prov.phone_id[i][0]) slot = i;
  if (slot < 0 || strlen(id) > 8 || strlen(key_hex) > 64) return -1;
  struct P {
    int i;
    const char *id, *key;
    uint8_t flat;
  } a{slot, id, key_hex, flat};
  bool ok = user_write(
      [](nvs_handle_t h, const void *v) {
        const P *a = static_cast<const P *>(v);
        char k[8];
        snprintf(k, sizeof k, "pid%d", a->i);
        nvs_set_str(h, k, a->id);
        snprintf(k, sizeof k, "pk%d", a->i);
        if (a->key[0]) nvs_set_str(h, k, a->key);
        else nvs_erase_key(h, k);
        snprintf(k, sizeof k, "pf%d", a->i);
        nvs_set_u8(h, k, a->flat);
      },
      &a);
  if (!ok) return -1;
  if (!prov.phone_id[slot][0]) prov.phones++;
  snprintf(prov.phone_id[slot], sizeof prov.phone_id[slot], "%s", id);
  snprintf(prov.phone_key[slot], sizeof prov.phone_key[slot], "%s", key_hex);
  prov.phone_flat[slot] = flat;
  return slot;
}

bool cb_prov_phone_forget(const char *id, uint8_t flat) {
  for (int i = 0; i < CbProvisioning::kPhoneSlots; i++) {
    if (!prov.phone_id[i][0] || prov.phone_flat[i] != flat || strcmp(prov.phone_id[i], id) != 0) continue;
    user_write(
        [](nvs_handle_t h, const void *v) {
          int i = *static_cast<const int *>(v);
          char k[8];
          for (const char *f : {"pid%d", "pk%d", "pf%d"}) {
            snprintf(k, sizeof k, f, i);
            nvs_erase_key(h, k);
          }
        },
        &i);
    prov.phone_id[i][0] = 0;
    prov.phone_key[i][0] = 0;
    prov.phones--;
    return true;
  }
  return false;
}
