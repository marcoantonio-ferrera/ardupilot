#include "RCOutput_FabricServo.h"

#ifdef CONFIG_AP_HAL_ZEPHYR_FABRIC_SERVO_PWM

#include <algorithm>
#include <cstring>

#include <zephyr/sys/sys_io.h>

using namespace Zephyr;

static constexpr mm_reg_t SERVO_BASE =
    static_cast<mm_reg_t>(DT_REG_ADDR(DT_NODELABEL(fabric_servo_pwm)));

void RCOutputFabricServo::init()
{
    _values.fill(0u);
    _enabled.fill(false);
    _corked       = false;
    _pending_mask = 0u;

    for (uint8_t ch = 0u; ch < NUM_CHANNELS; ch++) {
        sys_write32(0u, SERVO_BASE + static_cast<uint32_t>(ch) * 4u);
    }
}

void RCOutputFabricServo::_write_hw(uint8_t ch)
{
    if (ch >= NUM_CHANNELS) {
        return;
    }
    uint32_t hw_val = 0u;
    if (_enabled[ch] && _values[ch] > OFFSET_US) {
        hw_val = static_cast<uint32_t>(_values[ch]) - OFFSET_US;
        if (hw_val > MAX_HW_VAL) {
            hw_val = MAX_HW_VAL;
        }
    }
    sys_write32(hw_val, SERVO_BASE + static_cast<uint32_t>(ch) * 4u);
}

void RCOutputFabricServo::set_freq(uint32_t /*chmask*/, uint16_t /*freq_hz*/)
{
    // period is fixed at 50 Hz in hardware
}

uint16_t RCOutputFabricServo::get_freq(uint8_t /*chan*/)
{
    return 50u;
}

void RCOutputFabricServo::enable_ch(uint8_t ch)
{
    if (ch >= NUM_CHANNELS) {
        return;
    }
    _enabled[ch] = true;
}

void RCOutputFabricServo::disable_ch(uint8_t ch)
{
    if (ch >= NUM_CHANNELS) {
        return;
    }
    _enabled[ch] = false;
    _write_hw(ch);
}

void RCOutputFabricServo::write(uint8_t ch, uint16_t period_us)
{
    if (ch >= NUM_CHANNELS) {
        return;
    }
    _values[ch] = period_us;

    if (_corked) {
        _pending_mask |= static_cast<uint8_t>(1u << ch);
    } else {
        _write_hw(ch);
    }
}

uint16_t RCOutputFabricServo::read(uint8_t ch)
{
    if (ch < NUM_CHANNELS) {
        return _values[ch];
    }
    return 0u;
}

void RCOutputFabricServo::read(uint16_t *period_us, uint8_t len)
{
    const size_t count = std::min<size_t>(len, NUM_CHANNELS);
    memcpy(period_us, _values.data(), count * sizeof(uint16_t));
}

void RCOutputFabricServo::cork()
{
    _corked = true;
}

void RCOutputFabricServo::push()
{
    if (!_corked) {
        return;
    }
    _corked = false;

    uint8_t mask = _pending_mask;
    _pending_mask = 0u;

    for (uint8_t ch = 0u; ch < NUM_CHANNELS && mask != 0u; ch++) {
        if (mask & (1u << ch)) {
            _write_hw(ch);
            mask &= static_cast<uint8_t>(~(1u << ch));
        }
    }
}

#endif
