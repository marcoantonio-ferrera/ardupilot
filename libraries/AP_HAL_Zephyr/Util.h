#pragma once

#include <AP_HAL/AP_HAL.h>
#include "AP_HAL_Zephyr_Namespace.h"

class Zephyr::Util : public AP_HAL::Util {
public:
    void set_hw_rtc(uint64_t time_utc_usec) override;
    uint64_t get_hw_rtc() const override;
    void commandline_arguments(uint8_t &argc, char *const *&argv) override;
    uint32_t available_memory() override;

private:
    uint64_t _rtc_offset_us = 0;
};