#include "dev_cal.h"

#ifdef CONFIG_CARL_DEV_CAL

#include <cstring>

#include <zephyr/console/console.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include "sensors.h"

namespace carl::dev_cal {

namespace {

// Pending endpoints captured this session, committed to NVS on CAL SAVE.
uint16_t g_dry = 0;
uint16_t g_wet = 0;

void handle(const char* line) {
    if (std::strcmp(line, "CAL DRY") == 0) {
        uint16_t v = 0;
        if (carl::sensors::readSoilAveraged(&v)) {
            g_dry = v;
            printk("dev-cal: captured dry=%u\n", v);
        } else {
            printk("dev-cal: dry read failed\n");
        }
    } else if (std::strcmp(line, "CAL WET") == 0) {
        uint16_t v = 0;
        if (carl::sensors::readSoilAveraged(&v)) {
            g_wet = v;
            printk("dev-cal: captured wet=%u\n", v);
        } else {
            printk("dev-cal: wet read failed\n");
        }
    } else if (std::strcmp(line, "CAL SAVE") == 0) {
        if (g_dry != 0 && g_wet != 0 && g_dry != g_wet) {
            carl::sensors::setSoilCalibration(g_dry, g_wet);
            printk("dev-cal: saved dry=%u wet=%u\n", g_dry, g_wet);
        } else {
            printk("dev-cal: need CAL DRY and CAL WET first, and they must differ\n");
        }
    } else if (std::strcmp(line, "CAL SHOW") == 0) {
        uint16_t raw = 0;
        carl::sensors::readSoilRaw(&raw);
        printk("dev-cal: live=%u  pending dry=%u wet=%u  stored dry=%u wet=%u calibrated=%d\n",
               raw, g_dry, g_wet,
               carl::sensors::soilDryRaw(), carl::sensors::soilWetRaw(),
               carl::sensors::hasSoilCalibration());
    } else if (line[0] != '\0') {
        printk("dev-cal: unknown '%s' (CAL DRY | CAL WET | CAL SAVE | CAL SHOW)\n", line);
    }
}

void threadFn(void*, void*, void*) {
    console_getline_init();
    printk("dev-cal: ready — CAL DRY | CAL WET | CAL SAVE | CAL SHOW\n");
    while (true) {
        char* line = console_getline();   // blocks until a full line
        if (line != nullptr) handle(line);
    }
}

K_THREAD_STACK_DEFINE(g_stack, 1280);
struct k_thread g_thread;

}  // namespace

void start() {
    k_thread_create(&g_thread, g_stack, K_THREAD_STACK_SIZEOF(g_stack),
                    threadFn, nullptr, nullptr, nullptr,
                    K_PRIO_PREEMPT(10), 0, K_NO_WAIT);
    k_thread_name_set(&g_thread, "dev_cal");
}

}  // namespace carl::dev_cal

#else  // !CONFIG_CARL_DEV_CAL

namespace carl::dev_cal {
void start() {}
}  // namespace carl::dev_cal

#endif
