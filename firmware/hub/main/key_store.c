#include "key_store.h"

#include <ctype.h>
#include <stdbool.h>
#include <string.h>

#include "esp_log.h"
#include "sdkconfig.h"

static const char *TAG = "carl-keys";

#define MAX_KEYS 8

typedef struct {
    uint8_t mac[6];          // little-endian (NimBLE convention)
    uint8_t key[16];
    bool    used;
} entry_t;

static entry_t s_table[MAX_KEYS];
static unsigned s_count = 0;

static int hexnibble(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// Parse "AA:BB:CC:DD:EE:FF" into a little-endian 6-byte buffer (i.e. byte 0
// of the buffer is the LAST hex pair, matching what NimBLE hands us in
// scan callbacks). Returns true on success.
static bool parse_mac_le(const char *s, uint8_t out[6]) {
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
    for (int i = 0; i < 6; ++i) out[i] = big_endian[5 - i];
    return true;
}

// Parse 32 hex chars into 16 bytes.
static bool parse_key_hex(const char *s, uint8_t out[16]) {
    if (s == NULL || strlen(s) != 32) return false;
    for (int i = 0; i < 16; ++i) {
        const int hi = hexnibble(s[i * 2]);
        const int lo = hexnibble(s[i * 2 + 1]);
        if (hi < 0 || lo < 0) return false;
        out[i] = (uint8_t)((hi << 4) | lo);
    }
    return true;
}

void carl_key_store_init(void) {
    memset(s_table, 0, sizeof(s_table));
    s_count = 0;

#ifdef CONFIG_CARL_TEST_NODE_MAC
    const char *mac_str = CONFIG_CARL_TEST_NODE_MAC;
    const char *key_str = CONFIG_CARL_TEST_NODE_KEY;
    if (mac_str[0] == '\0' || key_str[0] == '\0') {
        ESP_LOGW(TAG, "no test-node provisioned (CARL_TEST_NODE_MAC/_KEY empty)");
        return;
    }
    entry_t *e = &s_table[0];
    if (!parse_mac_le(mac_str, e->mac)) {
        ESP_LOGE(TAG, "CARL_TEST_NODE_MAC '%s' not in AA:BB:CC:DD:EE:FF form", mac_str);
        return;
    }
    if (!parse_key_hex(key_str, e->key)) {
        ESP_LOGE(TAG, "CARL_TEST_NODE_KEY not 32 hex chars");
        return;
    }
    e->used = true;
    s_count = 1;
    ESP_LOGI(TAG, "test node %s provisioned", mac_str);
#endif

#ifdef CONFIG_CARL_TEST_NODE2_MAC
    const char *mac2_str = CONFIG_CARL_TEST_NODE2_MAC;
    const char *key2_str = CONFIG_CARL_TEST_NODE2_KEY;
    if (mac2_str[0] != '\0' && key2_str[0] != '\0') {
        entry_t *e2 = &s_table[s_count];
        if (!parse_mac_le(mac2_str, e2->mac)) {
            ESP_LOGE(TAG, "CARL_TEST_NODE2_MAC '%s' not in AA:BB:CC:DD:EE:FF form", mac2_str);
        } else if (!parse_key_hex(key2_str, e2->key)) {
            ESP_LOGE(TAG, "CARL_TEST_NODE2_KEY not 32 hex chars");
        } else {
            e2->used = true;
            s_count++;
            ESP_LOGI(TAG, "test node 2 %s provisioned", mac2_str);
        }
    }
#endif
}

const uint8_t *carl_key_store_lookup(const uint8_t mac6_le[6]) {
    for (unsigned i = 0; i < MAX_KEYS; ++i) {
        if (s_table[i].used && memcmp(s_table[i].mac, mac6_le, 6) == 0) {
            return s_table[i].key;
        }
    }
    return NULL;
}

unsigned carl_key_store_count(void) { return s_count; }
