#include "key_store.h"

#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "nvs.h"
#include "sdkconfig.h"

static const char *TAG = "carl-keys";

#define MAX_KEYS 16
#define NVS_NAMESPACE "carl-keys"

typedef struct {
    uint8_t mac[6];          // little-endian (NimBLE convention)
    uint8_t key[16];
    bool    used;
} entry_t;

static entry_t s_table[MAX_KEYS];
static unsigned s_count = 0;

// ---------- parsing helpers (also used by http_api.c) ----------

static int hexnibble(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

bool carl_key_store_parse_mac(const char *s, uint8_t out_le[6]) {
    if (s == NULL || strlen(s) != 17) return false;
    uint8_t big_endian[6];
    int byte_idx = 0;
    for (int i = 0; i < 17; ++i) {
        if ((i % 3) == 2) {
            if (s[i] != ':') return false;
            continue;
        }
        const int hi = hexnibble(s[i]);
        const int lo = hexnibble(s[i + 1]);
        if (hi < 0 || lo < 0) return false;
        big_endian[byte_idx++] = (uint8_t)((hi << 4) | lo);
        i++;  // consumed two hex chars
    }
    // Reverse for little-endian representation.
    for (int i = 0; i < 6; ++i) out_le[i] = big_endian[5 - i];
    return true;
}

bool carl_key_store_parse_key(const char *s, uint8_t out[16]) {
    if (s == NULL || strlen(s) != 32) return false;
    for (int i = 0; i < 16; ++i) {
        const int hi = hexnibble(s[i * 2]);
        const int lo = hexnibble(s[i * 2 + 1]);
        if (hi < 0 || lo < 0) return false;
        out[i] = (uint8_t)((hi << 4) | lo);
    }
    return true;
}

// ---------- NVS persistence ----------
// Each key is stored as an NVS blob named "k0".."k15" (matching MAX_KEYS).
// The blob is 22 bytes: 6 MAC (LE) + 16 AES key.

static void nvs_key_name(int slot, char out[8]) {
    snprintf(out, 8, "k%d", slot);
}

static void load_from_nvs(void) {
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &h) != ESP_OK) {
        ESP_LOGI(TAG, "no NVS namespace '%s' yet — first boot", NVS_NAMESPACE);
        return;
    }
    int loaded = 0;
    for (int i = 0; i < MAX_KEYS; ++i) {
        char name[8];
        nvs_key_name(i, name);
        uint8_t buf[22];
        size_t len = sizeof(buf);
        if (nvs_get_blob(h, name, buf, &len) == ESP_OK && len == 22) {
            memcpy(s_table[i].mac, buf, 6);
            memcpy(s_table[i].key, buf + 6, 16);
            s_table[i].used = true;
            s_count++;
            loaded++;
        }
    }
    nvs_close(h);
    if (loaded > 0) {
        ESP_LOGI(TAG, "loaded %d key(s) from NVS", loaded);
    }
}

static bool persist_slot(int slot) {
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h) != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open failed for write");
        return false;
    }
    uint8_t buf[22];
    memcpy(buf, s_table[slot].mac, 6);
    memcpy(buf + 6, s_table[slot].key, 16);
    char name[8];
    nvs_key_name(slot, name);
    esp_err_t err = nvs_set_blob(h, name, buf, 22);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS write slot %d failed: %s", slot, esp_err_to_name(err));
        return false;
    }
    return true;
}

static bool erase_slot(int slot) {
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h) != ESP_OK) return false;
    char name[8];
    nvs_key_name(slot, name);
    esp_err_t err = nvs_erase_key(h, name);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err == ESP_OK;
}

// ---------- in-memory table helpers ----------

static int find_by_mac(const uint8_t mac6_le[6]) {
    for (int i = 0; i < MAX_KEYS; ++i) {
        if (s_table[i].used && memcmp(s_table[i].mac, mac6_le, 6) == 0) {
            return i;
        }
    }
    return -1;
}

static int find_free_slot(void) {
    for (int i = 0; i < MAX_KEYS; ++i) {
        if (!s_table[i].used) return i;
    }
    return -1;
}

// Add a menuconfig test node if its MAC isn't already in the table (loaded
// from NVS). Does NOT persist to NVS — menuconfig keys are ephemeral
// "seed" entries for backwards compat during bring-up.
static void try_add_menuconfig(const char *mac_str, const char *key_str) {
    if (mac_str[0] == '\0' || key_str[0] == '\0') return;

    uint8_t mac_le[6], key[16];
    if (!carl_key_store_parse_mac(mac_str, mac_le)) {
        ESP_LOGE(TAG, "menuconfig MAC '%s' invalid", mac_str);
        return;
    }
    if (!carl_key_store_parse_key(key_str, key)) {
        ESP_LOGE(TAG, "menuconfig KEY invalid (need 32 hex chars)");
        return;
    }

    // Already in table (from NVS or earlier menuconfig entry)?
    if (find_by_mac(mac_le) >= 0) return;

    int slot = find_free_slot();
    if (slot < 0) {
        ESP_LOGW(TAG, "table full — can't add menuconfig node %s", mac_str);
        return;
    }
    memcpy(s_table[slot].mac, mac_le, 6);
    memcpy(s_table[slot].key, key, 16);
    s_table[slot].used = true;
    s_count++;
    ESP_LOGI(TAG, "menuconfig node %s added (slot %d, not persisted)", mac_str, slot);
}

// ---------- public API ----------

void carl_key_store_init(void) {
    memset(s_table, 0, sizeof(s_table));
    s_count = 0;

    // 1. Load persisted keys from NVS (survive reboot).
    load_from_nvs();

    // 2. Layer menuconfig test-node entries on top (backwards compat).
#ifdef CONFIG_CARL_TEST_NODE_MAC
    try_add_menuconfig(CONFIG_CARL_TEST_NODE_MAC, CONFIG_CARL_TEST_NODE_KEY);
#endif
#ifdef CONFIG_CARL_TEST_NODE2_MAC
    try_add_menuconfig(CONFIG_CARL_TEST_NODE2_MAC, CONFIG_CARL_TEST_NODE2_KEY);
#endif

    ESP_LOGI(TAG, "%u key(s) total after init", s_count);
}

const uint8_t *carl_key_store_lookup(const uint8_t mac6_le[6]) {
    int idx = find_by_mac(mac6_le);
    return (idx >= 0) ? s_table[idx].key : NULL;
}

unsigned carl_key_store_count(void) { return s_count; }

bool carl_key_store_add(const uint8_t mac6_le[6], const uint8_t key[16]) {
    // If MAC already exists, update the key in place.
    int slot = find_by_mac(mac6_le);
    if (slot >= 0) {
        memcpy(s_table[slot].key, key, 16);
        if (!persist_slot(slot)) return false;
        ESP_LOGI(TAG, "updated key for existing node (slot %d)", slot);
        return true;
    }

    slot = find_free_slot();
    if (slot < 0) {
        ESP_LOGW(TAG, "key store full (%d/%d)", MAX_KEYS, MAX_KEYS);
        return false;
    }

    memcpy(s_table[slot].mac, mac6_le, 6);
    memcpy(s_table[slot].key, key, 16);
    s_table[slot].used = true;
    if (!persist_slot(slot)) {
        // Roll back in-memory on NVS failure.
        s_table[slot].used = false;
        return false;
    }
    s_count++;
    ESP_LOGI(TAG, "provisioned new node (slot %d), %u total", slot, s_count);
    return true;
}

bool carl_key_store_remove(const uint8_t mac6_le[6]) {
    int slot = find_by_mac(mac6_le);
    if (slot < 0) return false;

    erase_slot(slot);  // best-effort NVS erase
    memset(&s_table[slot], 0, sizeof(entry_t));
    s_count--;
    ESP_LOGI(TAG, "removed node (slot %d), %u remaining", slot, s_count);
    return true;
}

void carl_key_store_foreach(carl_key_store_iter_fn fn, void *user) {
    for (int i = 0; i < MAX_KEYS; ++i) {
        if (s_table[i].used) {
            if (!fn(s_table[i].mac, user)) return;
        }
    }
}
