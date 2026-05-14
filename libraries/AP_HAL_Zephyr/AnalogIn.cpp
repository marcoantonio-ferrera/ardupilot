#include "AnalogIn.h"

#include <zephyr/devicetree.h>
#include <zephyr/sys/printk.h>

using namespace Zephyr;

AnalogSource::AnalogSource(AnalogIn *parent, int16_t pin) :
    _parent(parent),
    _pin(pin),
    _cached_v(0.0f),
    _last_sample_ms(0),
    _raw_buf(0),
    _configured(false)
{
}

void AnalogSource::_refresh()
{
    if (_pin == ANALOG_INPUT_BOARD_VCC) {
        return;
    }

    uint32_t now_ms = (uint32_t)(k_uptime_get() & 0xFFFFFFFF);
    if ((now_ms - _last_sample_ms) < SAMPLE_INTERVAL_MS) {
        return;
    }
    _last_sample_ms = now_ms;

    if (_pin < 0 || _pin >= 16) {
        return;
    }

    int16_t raw = 0;
    if (!_parent->sample_channel((uint8_t)_pin, &raw)) {
        return;
    }

    // apply ADC_GAIN_1 directly since adc_raw_to_millivolts() needs a bound driver
    int32_t val_mv = (int32_t)((int64_t)raw * _parent->vref_mv()
                               / ((1 << CONFIG_AP_HAL_ADC_BITS) - 1));
    _cached_v = (float)val_mv / 1000.0f;
}

float AnalogSource::voltage_latest()
{
    if (_pin == ANALOG_INPUT_BOARD_VCC) {
        return _parent->board_voltage();
    }
    _refresh();
    return _cached_v;
}

float AnalogSource::voltage_average()           { return voltage_latest(); }
float AnalogSource::voltage_average_ratiometric() { return voltage_average(); }
float AnalogSource::read_latest()               { return voltage_latest(); }
float AnalogSource::read_average()              { return read_latest(); }

bool AnalogSource::set_pin(uint8_t pin)
{
    if ((int16_t)pin == _pin) {
        return true;
    }
    _pin = (int16_t)pin;
    _configured = false;
    _cached_v = 0.0f;
    _last_sample_ms = 0;
    return true;
}

AnalogIn::AnalogIn() :
    _device(nullptr),
    _vref_mv(DEFAULT_VREF_MV),
    _channel_configured_mask(0)
{
    _channels.fill(nullptr);
}

void AnalogIn::init()
{
#if DT_HAS_ALIAS(ardupilot_adc0)
    _device = DEVICE_DT_GET(DT_ALIAS(ardupilot_adc0));
    if (!device_is_ready(_device)) {
        printk("AP_HAL_Zephyr: ADC device not ready, AnalogIn returns 0 V\n");
        _device = nullptr;
        return;
    }
    int32_t ref = adc_ref_internal(_device);
    if (ref > 0) {
        _vref_mv = ref;
    }
#endif
}

AP_HAL::AnalogSource *AnalogIn::channel(int16_t pin)
{
    for (AnalogSource *src : _channels) {
        if (src != nullptr && src->pin() == pin) {
            return src;
        }
    }
    for (AnalogSource *&slot : _channels) {
        if (slot == nullptr) {
            slot = NEW_NOTHROW AnalogSource(this, pin);
            return slot;
        }
    }
    return nullptr;
}

float AnalogIn::board_voltage()
{
    return DEFAULT_VREF_MV / 1000.0f;
}

bool AnalogIn::sample_channel(uint8_t channel_id, int16_t *raw_out)
{
    if (_device == nullptr) {
        return false;
    }

    if (!(_channel_configured_mask & BIT(channel_id))) {
        struct adc_channel_cfg cfg = {};
        cfg.gain             = ADC_GAIN_1;
        cfg.reference        = ADC_REF_VDD_1;
        cfg.acquisition_time = ADC_ACQ_TIME_DEFAULT;
        cfg.channel_id       = channel_id;

        if (adc_channel_setup(_device, &cfg) != 0) {
            printk("AP_HAL_Zephyr: ADC channel %u setup failed\n",
                   (unsigned)channel_id);
            return false;
        }
        _channel_configured_mask |= (uint16_t)BIT(channel_id);
    }

    int16_t buf = 0;
    struct adc_sequence seq = {};
    seq.channels    = BIT(channel_id);
    seq.buffer      = &buf;
    seq.buffer_size = sizeof(buf);
    seq.resolution  = ADC_RESOLUTION;

    if (adc_read(_device, &seq) != 0) {
        return false;
    }

    *raw_out = buf;
    return true;
}
