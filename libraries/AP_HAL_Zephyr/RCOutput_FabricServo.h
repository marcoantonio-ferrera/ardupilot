#pragma once
/*
 * RCOutput for the BeagleV-Fire ROBOTICS cape fabric servo block.
 * 8 channels, fixed 50 Hz, register = pulse_us - 900 (hw adds the 900 us base).
 */

#ifdef CONFIG_AP_HAL_ZEPHYR_FABRIC_SERVO_PWM

#include <array>
#include <cstdint>

#include <AP_HAL/AP_HAL.h>
#include "AP_HAL_Zephyr_Namespace.h"

class Zephyr::RCOutputFabricServo : public AP_HAL::RCOutput
{
public:
    void     init() override;
    void     set_freq(uint32_t chmask, uint16_t freq_hz) override;
    uint16_t get_freq(uint8_t chan) override;
    void     enable_ch(uint8_t ch) override;
    void     disable_ch(uint8_t ch) override;
    void     write(uint8_t ch, uint16_t period_us) override;
    uint16_t read(uint8_t ch) override;
    void     read(uint16_t *period_us, uint8_t len) override;
    void     cork() override;
    void     push() override;

private:
    static constexpr uint8_t  NUM_CHANNELS   = 8u;
    static constexpr uint32_t OFFSET_US      = 900u;   // hw pulse base
    static constexpr uint32_t MAX_HW_VAL     = 1200u;  // caps pulse at 2.1 ms

    std::array<uint16_t, NUM_CHANNELS> _values{};
    std::array<bool,     NUM_CHANNELS> _enabled{};

    bool     _corked        = false;
    uint8_t  _pending_mask  = 0u;

    void _write_hw(uint8_t ch);
};

#endif
