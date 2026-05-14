#pragma once

#include <array>
#include <cstdint>

#include <AP_Common/AP_Common.h>
#include <AP_HAL/AP_HAL.h>
#include "AP_HAL_Zephyr_Namespace.h"

struct device;

class Zephyr::RCOutput : public AP_HAL::RCOutput {
public:
    RCOutput();

    void init() override;
    void set_freq(uint32_t chmask, uint16_t freq_hz) override;
    uint16_t get_freq(uint8_t chan) override;
    void enable_ch(uint8_t ch) override;
    void disable_ch(uint8_t ch) override;
    void write(uint8_t ch, uint16_t period_us) override;
    uint16_t read(uint8_t ch) override;
    void read(uint16_t *period_us, uint8_t len) override;
    void cork() override;
    void push() override;

private:
    std::array<uint16_t, 16> _values;
    std::array<bool, 16>     _enabled;

    uint16_t _frequency_hz;
    uint32_t _period_ns;  // cached from _frequency_hz

    bool     _corked;
    uint32_t _pending_mask;

    // resolved from the ardupilot-pwm0 DT alias; null in sim
    const struct device *_pwm_dev;
    bool _has_pwm;

    void _apply(uint8_t ch);
};
