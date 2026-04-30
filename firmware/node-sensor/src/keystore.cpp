#include "keystore.h"

#include <cstring>

#include <zephyr/kernel.h>
#include <zephyr/random/random.h>
#include <zephyr/settings/settings.h>
#include <zephyr/sys/printk.h>

namespace carl::keystore {

namespace {

constexpr const char* kKeyPath     = "carl/key";
constexpr const char* kCounterPath = "carl/counter";

// Persist the counter to flash every N broadcasts. Lower wears flash faster;
// higher means more counters get re-used after an unclean reset (which is
// safe — the hub rejects non-increasing counters, so a node will simply skip
// ahead by the checkpoint window after reboot).
constexpr uint32_t kCounterCheckpointEvery = 64;

uint8_t   g_key[16];
bool      g_have_key = false;
bool      g_fresh    = false;
uint32_t  g_counter  = 0;
uint32_t  g_counter_persisted = 0;

int settingsLoadCb(const char* name, size_t len, settings_read_cb read_cb,
                   void* cb_arg) {
    const char* next = nullptr;
    int name_len = settings_name_next(name, &next);

    if (name_len == 3 && std::strncmp(name, "key", 3) == 0 && len == sizeof(g_key)) {
        if (read_cb(cb_arg, g_key, sizeof(g_key)) == sizeof(g_key)) {
            g_have_key = true;
        }
        return 0;
    }
    if (name_len == 7 && std::strncmp(name, "counter", 7) == 0
        && len == sizeof(g_counter)) {
        read_cb(cb_arg, &g_counter_persisted, sizeof(g_counter_persisted));
        // Skip ahead a checkpoint window so we never reuse a counter after an
        // unclean reset.
        g_counter = g_counter_persisted + kCounterCheckpointEvery;
        return 0;
    }
    return 0;
}

SETTINGS_STATIC_HANDLER_DEFINE(carl_keystore, "carl",
                               /*get=*/nullptr,
                               /*set=*/settingsLoadCb,
                               /*commit=*/nullptr,
                               /*export=*/nullptr);

void persistCounter(uint32_t v) {
    settings_save_one(kCounterPath, &v, sizeof(v));
    g_counter_persisted = v;
}

}  // namespace

bool init() {
    if (!g_have_key) {
        // First boot: generate fresh key.
        sys_rand_get(g_key, sizeof(g_key));
        if (settings_save_one(kKeyPath, g_key, sizeof(g_key)) != 0) return false;
        g_have_key = true;
        g_fresh    = true;
        persistCounter(0);
    }
    return true;
}

const uint8_t* key() { return g_key; }
bool isFreshKey() { return g_fresh; }

void keyHex(char out[33]) {
    static const char* h = "0123456789ABCDEF";
    for (int i = 0; i < 16; ++i) {
        out[i * 2 + 0] = h[(g_key[i] >> 4) & 0x0F];
        out[i * 2 + 1] = h[g_key[i] & 0x0F];
    }
    out[32] = '\0';
}

uint32_t nextCounter() {
    const uint32_t c = g_counter++;
    if (c - g_counter_persisted >= kCounterCheckpointEvery) {
        persistCounter(g_counter);
    }
    return c;
}

}  // namespace carl::keystore
