// cb_nvs_secure.cpp - see cb_nvs_secure.h.
#include "cb_nvs_secure.h"

#include <Arduino.h>
#include <esp_efuse.h>
#include <esp_hmac.h>
#include <esp_partition.h>
#include <esp_random.h>
#include <mbedtls/sha256.h>
#include <nvs.h>
#include <nvs_flash.h>
#include <nvs_sec_provider.h>

#include <cstring>
#include <string>
#include <vector>

#include "cb_console.h"
#include "cb_log.h"
#include "cb_provisioning.h"
#include "cb_setup.h"

namespace {

constexpr const char *kPart = "nvs";
constexpr char kMagic[8] = {'C', 'B', 'N', 'V', 'S', 'E', 'N', 'C'};
constexpr uint32_t kMarkerVersion = 1;

struct Marker {
  char magic[8];
  uint32_t version;
  uint8_t print[8];  // first 8 bytes of SHA-256(eky | tky): which key encrypted the data, nothing about the key
  uint32_t check;    // simple sum over the bytes above, against half-written sectors
};

enum class Mode { Plain, Encrypted, Migrated, OtherKeyErased, KeyMissing, Failed };
Mode mode = Mode::Plain;
const char *key_source = "none";  // "efuse", "test", "none"
int migrated_entries = -1;
esp_err_t last_err = ESP_OK;
nvs_sec_scheme_t *scheme = nullptr;

uint32_t marker_check(const Marker &m) {
  uint32_t s = 0;
  const uint8_t *p = reinterpret_cast<const uint8_t *>(&m);
  for (size_t i = 0; i < offsetof(Marker, check); i++) s = s * 31 + p[i];
  return s;
}

void key_print(const nvs_sec_cfg_t &cfg, uint8_t out[8]) {
  uint8_t h[32];
  mbedtls_sha256_context c;
  mbedtls_sha256_init(&c);
  mbedtls_sha256_starts(&c, 0);
  mbedtls_sha256_update(&c, cfg.eky, sizeof cfg.eky);
  mbedtls_sha256_update(&c, cfg.tky, sizeof cfg.tky);
  mbedtls_sha256_finish(&c, h);
  mbedtls_sha256_free(&c);
  memcpy(out, h, 8);
}

const esp_partition_t *marker_part() {
  return esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_NVS_KEYS, nullptr);
}

bool read_marker(Marker *m) {
  const esp_partition_t *p = marker_part();
  return p && esp_partition_read(p, 0, m, sizeof *m) == ESP_OK && !memcmp(m->magic, kMagic, 8) &&
         m->version == kMarkerVersion && m->check == marker_check(*m);
}

bool write_marker(const nvs_sec_cfg_t &cfg) {
  const esp_partition_t *p = marker_part();
  if (!p) return false;
  Marker m = {};
  memcpy(m.magic, kMagic, 8);
  m.version = kMarkerVersion;
  key_print(cfg, m.print);
  m.check = marker_check(m);
  return esp_partition_erase_range(p, 0, p->erase_size) == ESP_OK && esp_partition_write(p, 0, &m, sizeof m) == ESP_OK;
}

#ifdef CB_HB_TESTKEY
bool erase_marker() {
  const esp_partition_t *p = marker_part();
  return p && esp_partition_erase_range(p, 0, p->erase_size) == ESP_OK;
}
#endif

// The key: from the HMAC peripheral (eFuse BLOCK_KEY0, purpose HMAC_UP) - reading never burns anything - or, in the
// test build, from software.
bool get_keys(nvs_sec_cfg_t *cfg) {
#ifdef CB_HB_TESTKEY
  const char *e = "ClearBell HB test key E (never in a product)", *t = "ClearBell HB test key T (never in a product)";
  mbedtls_sha256(reinterpret_cast<const unsigned char *>(e), strlen(e), cfg->eky, 0);
  mbedtls_sha256(reinterpret_cast<const unsigned char *>(t), strlen(t), cfg->tky, 0);
  key_source = "test";
  return true;
#else
  if (!scheme) {
    nvs_sec_config_hmac_t hc = {};
    hc.hmac_key_id = HMAC_KEY0;
    if (nvs_sec_provider_register_hmac(&hc, &scheme) != ESP_OK) return false;
  }
  if (nvs_flash_read_security_cfg_v2(scheme, cfg) != ESP_OK) return false;  // no HMAC_UP key in BLOCK_KEY0
  key_source = "efuse";
  return true;
#endif
}

// ---- Copying every entry of "nvs" (migration both ways) --------------------------------------------------------
struct Item {
  std::string ns, key;
  nvs_type_t type;
  std::vector<uint8_t> data;
};

bool read_all(std::vector<Item> &items) {
  nvs_iterator_t it = nullptr;
  esp_err_t e = nvs_entry_find(kPart, nullptr, NVS_TYPE_ANY, &it);
  while (e == ESP_OK) {
    nvs_entry_info_t info;
    nvs_entry_info(it, &info);
    nvs_handle_t h;
    if (nvs_open_from_partition(kPart, info.namespace_name, NVS_READONLY, &h) == ESP_OK) {
      Item x{info.namespace_name, info.key, info.type, {}};
      size_t len = 0;
      bool ok = true;
      switch (info.type) {
        case NVS_TYPE_U8: x.data.resize(1); ok = nvs_get_u8(h, info.key, x.data.data()) == ESP_OK; break;
        case NVS_TYPE_I8: x.data.resize(1); ok = nvs_get_i8(h, info.key, (int8_t *)x.data.data()) == ESP_OK; break;
        case NVS_TYPE_U16: x.data.resize(2); ok = nvs_get_u16(h, info.key, (uint16_t *)x.data.data()) == ESP_OK; break;
        case NVS_TYPE_I16: x.data.resize(2); ok = nvs_get_i16(h, info.key, (int16_t *)x.data.data()) == ESP_OK; break;
        case NVS_TYPE_U32: x.data.resize(4); ok = nvs_get_u32(h, info.key, (uint32_t *)x.data.data()) == ESP_OK; break;
        case NVS_TYPE_I32: x.data.resize(4); ok = nvs_get_i32(h, info.key, (int32_t *)x.data.data()) == ESP_OK; break;
        case NVS_TYPE_U64: x.data.resize(8); ok = nvs_get_u64(h, info.key, (uint64_t *)x.data.data()) == ESP_OK; break;
        case NVS_TYPE_I64: x.data.resize(8); ok = nvs_get_i64(h, info.key, (int64_t *)x.data.data()) == ESP_OK; break;
        case NVS_TYPE_STR:
          ok = nvs_get_str(h, info.key, nullptr, &len) == ESP_OK;
          x.data.resize(len);
          ok = ok && nvs_get_str(h, info.key, (char *)x.data.data(), &len) == ESP_OK;
          break;
        case NVS_TYPE_BLOB:
          ok = nvs_get_blob(h, info.key, nullptr, &len) == ESP_OK;
          x.data.resize(len);
          ok = ok && (len == 0 || nvs_get_blob(h, info.key, x.data.data(), &len) == ESP_OK);
          break;
        default: ok = false;
      }
      nvs_close(h);
      if (!ok) {
        nvs_release_iterator(it);
        return false;
      }
      items.push_back(std::move(x));
    }
    e = nvs_entry_next(&it);
  }
  nvs_release_iterator(it);
  return e == ESP_ERR_NVS_NOT_FOUND;
}

bool write_all(const std::vector<Item> &items) {
  for (const Item &x : items) {
    nvs_handle_t h;
    if (nvs_open_from_partition(kPart, x.ns.c_str(), NVS_READWRITE, &h) != ESP_OK) return false;
    const char *k = x.key.c_str();
    const void *d = x.data.data();
    esp_err_t e;
    switch (x.type) {
      case NVS_TYPE_U8: e = nvs_set_u8(h, k, *(const uint8_t *)d); break;
      case NVS_TYPE_I8: e = nvs_set_i8(h, k, *(const int8_t *)d); break;
      case NVS_TYPE_U16: e = nvs_set_u16(h, k, *(const uint16_t *)d); break;
      case NVS_TYPE_I16: e = nvs_set_i16(h, k, *(const int16_t *)d); break;
      case NVS_TYPE_U32: e = nvs_set_u32(h, k, *(const uint32_t *)d); break;
      case NVS_TYPE_I32: e = nvs_set_i32(h, k, *(const int32_t *)d); break;
      case NVS_TYPE_U64: e = nvs_set_u64(h, k, *(const uint64_t *)d); break;
      case NVS_TYPE_I64: e = nvs_set_i64(h, k, *(const int64_t *)d); break;
      case NVS_TYPE_STR: e = nvs_set_str(h, k, (const char *)d); break;
      case NVS_TYPE_BLOB: e = nvs_set_blob(h, k, d, x.data.size()); break;
      default: e = ESP_ERR_INVALID_ARG;
    }
    if (e == ESP_OK) e = nvs_commit(h);
    nvs_close(h);
    if (e != ESP_OK) return false;
  }
  return true;
}

void wipe(std::vector<Item> &items) {
  for (Item &x : items) std::fill(x.data.begin(), x.data.end(), 0);
}

}  // namespace

extern "C" esp_err_t __real_nvs_flash_init(void);

// Called by the Arduino core before setup() (and by anyone else calling nvs_flash_init). No logging possible yet:
// the outcome is kept and logged by cb_nvs_secure_begin().
extern "C" esp_err_t __wrap_nvs_flash_init(void) {
  nvs_sec_cfg_t cfg = {};
  Marker m;
  bool have = get_keys(&cfg), marked = read_marker(&m);
  esp_err_t e;
  if (!have) {
    if (marked) {  // encrypted data, but no key in this build/chip: a plain open would erase it
      mode = Mode::KeyMissing;
      return last_err = ESP_ERR_NVS_NOT_INITIALIZED;
    }
    mode = Mode::Plain;
    return last_err = __real_nvs_flash_init();
  }
  uint8_t print[8];
  key_print(cfg, print);
  if (marked && !memcmp(m.print, print, 8)) {
    e = nvs_flash_secure_init(&cfg);
    mode = e == ESP_OK ? Mode::Encrypted : Mode::Failed;
  } else if (marked) {  // encrypted with another key (e.g. the test key): unreadable, start empty
    e = nvs_flash_erase_partition(kPart);
    if (e == ESP_OK) e = nvs_flash_secure_init(&cfg);
    if (e == ESP_OK) e = write_marker(cfg) ? ESP_OK : ESP_FAIL;
    mode = e == ESP_OK ? Mode::OtherKeyErased : Mode::Failed;
  } else {  // plain data and a key: migrate (HB-E2). Power loss in here = empty NVS = setup mode, nothing worse.
    std::vector<Item> items;
    e = __real_nvs_flash_init();
    bool ok = e == ESP_OK && read_all(items);
    if (e == ESP_ERR_NVS_NO_FREE_PAGES || e == ESP_ERR_NVS_NEW_VERSION_FOUND) ok = true;  // nothing readable to keep
    nvs_flash_deinit_partition(kPart);
    e = ok ? nvs_flash_erase_partition(kPart) : ESP_FAIL;
    if (e == ESP_OK) e = nvs_flash_secure_init(&cfg);
    if (e == ESP_OK) e = write_all(items) ? ESP_OK : ESP_FAIL;
    if (e == ESP_OK) e = write_marker(cfg) ? ESP_OK : ESP_FAIL;
    migrated_entries = (int)items.size();
    wipe(items);
    mode = e == ESP_OK ? Mode::Migrated : Mode::Failed;
  }
  memset(&cfg, 0, sizeof cfg);
  return last_err = e;
}

namespace {

void cmd_haertung(const char *args) {
  const CbProvisioning &p = cb_prov();
  if (!*args) {
    Marker m;
    cb_log("haertung: storage %s (key source %s), marker %s; eFuse KEY0 purpose %d, %s; secure version %lu%s",
           cb_nvs_secure_state(), key_source, read_marker(&m) ? "present" : "none",
           (int)esp_efuse_get_key_purpose(EFUSE_BLK_KEY0), esp_efuse_key_block_unused(EFUSE_BLK_KEY0) ? "unused" : "USED",
           (unsigned long)esp_efuse_read_secure_version(),
#if CONFIG_EFUSE_VIRTUAL
           " - VIRTUAL eFuses (emulation build)"
#else
           ""
#endif
    );
    cb_log("usage: haertung nvs-schluessel BRENNEN <unit number>   (burns the NVS key into eFuse KEY0 - FOREVER)");
#ifdef CB_HB_TESTKEY
    cb_log("       haertung klartext JA   (test build: decrypt the storage again, then flash a normal build)");
#endif
    return;
  }
  char a[24] = {}, b[24] = {}, c[24] = {};
  sscanf(args, "%23s %23s %23s", a, b, c);
  if (!strcmp(a, "nvs-schluessel")) {
#ifdef CB_HB_TESTKEY
    cb_log("haertung: refused - this is the test build (software key); burn only with a normal build");
#else
    if (strcmp(b, "BRENNEN") != 0 || strcmp(c, p.serial) != 0) {
      cb_log("haertung: refused - confirm with: haertung nvs-schluessel BRENNEN %s", p.serial);
      return;
    }
    if (!esp_efuse_key_block_unused(EFUSE_BLK_KEY0) || esp_efuse_get_key_purpose(EFUSE_BLK_KEY0) != ESP_EFUSE_KEY_PURPOSE_USER) {
      cb_log("haertung: refused - eFuse KEY0 is not empty (purpose %d)", (int)esp_efuse_get_key_purpose(EFUSE_BLK_KEY0));
      return;
    }
    nvs_sec_config_hmac_t hc = {};
    hc.hmac_key_id = HMAC_KEY0;
    if (!scheme && nvs_sec_provider_register_hmac(&hc, &scheme) != ESP_OK) {
      cb_log("haertung: HMAC scheme not available");
      return;
    }
    nvs_sec_cfg_t cfg = {};
    esp_err_t e = nvs_flash_generate_keys_v2(scheme, &cfg);  // random key on the chip -> KEY0, purpose HMAC_UP, locked
    memset(&cfg, 0, sizeof cfg);
    // Burning and deriving are two steps: report what the eFuse says, not only the return code (HB.6 found the gap).
    bool burned = esp_efuse_get_key_purpose(EFUSE_BLK_KEY0) == ESP_EFUSE_KEY_PURPOSE_HMAC_UP;
    if (burned && e == ESP_OK) {
      cb_log("haertung: NVS key BURNED into eFuse KEY0 and usable - restarting, the storage is migrated at the start");
      cb_setup_reboot_in(2000);
    } else if (burned) {
      cb_log("haertung: NVS key BURNED into eFuse KEY0, but deriving the storage key FAILED (%s) - no restart, "
             "storage stays plain; check 'haertung'%s", esp_err_to_name(e),
#if CONFIG_EFUSE_VIRTUAL
             " (expected with VIRTUAL eFuses: the HMAC peripheral sees the real ones)"
#else
             ""
#endif
      );
    } else {
      cb_log("haertung: NVS key NOT burned (%s)", esp_err_to_name(e));
    }
#endif
    return;
  }
#ifdef CB_HB_TESTKEY
  if (!strcmp(a, "klartext") && !strcmp(b, "JA")) {
    std::vector<Item> items;
    bool ok = read_all(items);
    if (ok) ok = nvs_flash_deinit_partition(kPart) == ESP_OK && nvs_flash_erase_partition(kPart) == ESP_OK &&
                 __real_nvs_flash_init() == ESP_OK && write_all(items) && erase_marker();
    cb_log("haertung: back to plain %s (%u entries) - flash a normal build now (USB), the test key would migrate again",
           ok ? "done" : "FAILED", (unsigned)items.size());
    wipe(items);
    return;
  }
#endif
  cb_log("haertung: unknown - type 'haertung' for help");
}

// Names, types, sizes and a CRC of every entry - never the values. For before/after comparisons (HB.3).
void cmd_nvs_liste(const char *) {
  std::vector<Item> items;
  bool ok = read_all(items);
  uint32_t all = 0;
  for (const Item &x : items) {
    uint32_t crc = 0;
    for (uint8_t v : x.data) crc = (crc << 5) + crc + v;  // djb-like, enough to see a change
    all = all * 33 + crc;
    cb_log("  %-15s %-15s type 0x%02x len %4u sum %08lx", x.ns.c_str(), x.key.c_str(), (unsigned)x.type,
           (unsigned)x.data.size(), (unsigned long)crc);
    delay(15);  // the log never blocks and drops lines in a burst - pace them so the list arrives complete
  }
  cb_log("nvs-liste: %u entries, total sum %08lx%s, storage %s", (unsigned)items.size(), (unsigned long)all,
         ok ? "" : " (READ ERROR)", cb_nvs_secure_state());
  wipe(items);
}

}  // namespace

void cb_nvs_secure_begin() {
  cb_console_add("haertung", "haertung - storage encryption and eFuse state (Härtung Stufe B)", cmd_haertung);
  cb_console_add("nvs-liste", "list every NVS entry (namespace, key, type, length, sum - no values)", cmd_nvs_liste);
  if (mode == Mode::Migrated)
    cb_log("nvs: storage MIGRATED to encrypted (%d entries, key %s)", migrated_entries, key_source);
  else if (mode == Mode::KeyMissing)
    cb_log("nvs: storage is ENCRYPTED but no key here - NOT opened (a plain open would erase it); unit stays unconfigured");
  else if (mode == Mode::Failed)
    cb_log("nvs: encrypted storage FAILED (%s) - unit stays unconfigured", esp_err_to_name(last_err));
  else
    cb_log("nvs: storage %s (key %s)", cb_nvs_secure_state(), key_source);
}

bool cb_nvs_secure_encrypted() {
  return mode == Mode::Encrypted || mode == Mode::Migrated || mode == Mode::OtherKeyErased;
}

const char *cb_nvs_secure_state() {
  switch (mode) {
    case Mode::Plain: return "plain";
    case Mode::Encrypted: return "encrypted";
    case Mode::Migrated: return "encrypted (migrated at this start)";
    case Mode::OtherKeyErased: return "encrypted (data of another key erased)";
    case Mode::KeyMissing: return "encrypted, KEY MISSING";
    case Mode::Failed: return "FAILED";
  }
  return "?";
}

void cb_nvs_secure_json(cb::Json &j) {
  j.kv("nvs_encrypted", cb_nvs_secure_encrypted());
  j.kv("nvs_state", cb_nvs_secure_state());
  j.kv("nvs_key", key_source);
  j.kv("efuse_secure_version", (unsigned long)esp_efuse_read_secure_version());
}
