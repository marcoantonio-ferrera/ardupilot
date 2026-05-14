#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include <cstdarg>
#include <cstdio>

#include <AP_HAL/AP_HAL.h>
#include "HAL_Zephyr_Class.h"

namespace AP_HAL {

void init()
{
}

void panic(const char *errormsg, ...)
{
    char buffer[256];
    va_list ap;

    va_start(ap, errormsg);
    vsnprintf(buffer, sizeof(buffer), errormsg, ap);
    va_end(ap);

    printk("AP_HAL panic: %s\n", buffer);

    for (;;) {
        k_sleep(K_FOREVER);
    }
}

uint32_t micros()
{
    return static_cast<uint32_t>(micros64());
}

uint32_t millis()
{
    return static_cast<uint32_t>(millis64());
}

uint64_t micros64()
{
    return k_ticks_to_us_floor64(k_uptime_ticks());
}

uint64_t millis64()
{
    return k_uptime_get();
}

} // namespace AP_HAL

static HAL_Zephyr hal_zephyr;

const AP_HAL::HAL &AP_HAL::get_HAL()
{
    return hal_zephyr;
}

AP_HAL::HAL &AP_HAL::get_HAL_mutable()
{
    return hal_zephyr;
}