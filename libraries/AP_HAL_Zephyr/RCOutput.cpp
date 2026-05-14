#include "RCOutput.h"

#include <algorithm>
#include <cstring>

#ifdef CONFIG_PWM
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/pwm.h>
#endif

using namespace Zephyr;

static constexpr uint16_t DEFAULT_FREQ_HZ  = 50u;
static constexpr uint32_t NS_PER_US        = 1000u;
static constexpr uint32_t NS_PER_S         = 1000000000u;
static constexpr uint16_t NUM_CHANNELS     = 16u;

RCOutput::RCOutput()
    : _values{}
    , _enabled{}
    , _frequency_hz(DEFAULT_FREQ_HZ)
    , _period_ns(NS_PER_S / DEFAULT_FREQ_HZ)
    , _corked(false)
    , _pending_mask(0u)
    , _pwm_dev(nullptr)
    , _has_pwm(false)
{
}

void RCOutput::init()
{
    _values.fill(0u);
    _enabled.fill(false);
    _corked       = false;
    _pending_mask = 0u;

#ifdef CONFIG_PWM
#if DT_HAS_ALIAS(ardupilot_pwm0) && DT_NODE_HAS_STATUS(DT_ALIAS(ardupilot_pwm0), okay)
    _pwm_dev = DEVICE_DT_GET(DT_ALIAS(ardupilot_pwm0));
    _has_pwm = device_is_ready(_pwm_dev);
    if (!_has_pwm) { _pwm_dev = nullptr; }
#endif
#endif
}

void RCOutput::_apply(uint8_t ch)
{
#ifdef CONFIG_PWM
    if (!_has_pwm || ch >= NUM_CHANNELS) {
        return;
    }
    const uint32_t pulse_ns = _enabled[ch]
                              ? static_cast<uint32_t>(_values[ch]) * NS_PER_US
                              : 0u;
    pwm_set(_pwm_dev, ch, _period_ns, pulse_ns, PWM_POLARITY_NORMAL);
#endif
}

void RCOutput::set_freq(uint32_t chmask, uint16_t freq_hz)
{
    if (freq_hz == 0u) {
        return;
    }
    _frequency_hz = freq_hz;
    _period_ns    = NS_PER_S / static_cast<uint32_t>(freq_hz);

    for (uint8_t ch = 0u; ch < NUM_CHANNELS; ch++) {
        if ((chmask & (1u << ch)) && _enabled[ch]) {
            _apply(ch);
        }
    }
}

uint16_t RCOutput::get_freq(uint8_t chan)
{
    (void)chan;
    return _frequency_hz;
}

void RCOutput::enable_ch(uint8_t ch)
{
    if (ch >= NUM_CHANNELS) {
        return;
    }
    _enabled[ch] = true;
}

void RCOutput::disable_ch(uint8_t ch)
{
    if (ch >= NUM_CHANNELS) {
        return;
    }
    _enabled[ch] = false;
    _apply(ch);
}

void RCOutput::write(uint8_t ch, uint16_t period_us)
{
    if (ch >= NUM_CHANNELS) {
        return;
    }
    _values[ch] = period_us;

    if (_corked) {
        _pending_mask |= (1u << ch);
    } else {
        _apply(ch);
    }
}

uint16_t RCOutput::read(uint8_t ch)
{
    if (ch < NUM_CHANNELS) {
        return _values[ch];
    }
    return 0u;
}

void RCOutput::read(uint16_t *period_us, uint8_t len)
{
    const size_t count = std::min<size_t>(len, NUM_CHANNELS);
    memcpy(period_us, _values.data(), count * sizeof(uint16_t));
}

void RCOutput::cork()
{
    _corked = true;
}

void RCOutput::push()
{
    if (!_corked) {
        return;
    }
    _corked = false;

    uint32_t mask = _pending_mask;
    _pending_mask = 0u;

    for (uint8_t ch = 0u; ch < NUM_CHANNELS && mask != 0u; ch++) {
        if (mask & (1u << ch)) {
            _apply(ch);
            mask &= ~(1u << ch);
        }
    }
}
