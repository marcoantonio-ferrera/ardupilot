#include "Util.h"

#include <malloc.h>

#include <zephyr/kernel.h>

#include <AP_HAL_Zephyr/Scheduler.h>

using namespace Zephyr;

void Util::set_hw_rtc(uint64_t time_utc_usec)
{
    const uint64_t now_us = k_ticks_to_us_floor64(k_uptime_ticks());
    _rtc_offset_us = time_utc_usec - now_us;
}

uint64_t Util::get_hw_rtc() const
{
    return k_ticks_to_us_floor64(k_uptime_ticks()) + _rtc_offset_us;
}

void Util::commandline_arguments(uint8_t &argc, char *const *&argv)
{
    auto *scheduler = static_cast<Zephyr::Scheduler *>(AP_HAL::get_HAL_mutable().scheduler);
    scheduler->commandline_arguments(argc, argv);
}

// base class returns 4096 which trips EKF3's memory check unconditionally;
// give it a real free-heap figure so it proceeds to actually allocate.
uint32_t Util::available_memory()
{
    struct mallinfo mi = mallinfo();
    if (mi.fordblks > 0) {
        return (uint32_t)mi.fordblks;
    }
    return CONFIG_HEAP_MEM_POOL_SIZE;
}