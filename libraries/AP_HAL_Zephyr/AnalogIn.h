#pragma once

#include <array>

#include <zephyr/device.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/kernel.h>

#include <AP_HAL/AP_HAL.h>
#include <AP_Common/AP_Common.h>

#include "AP_HAL_Zephyr_Namespace.h"

class Zephyr::AnalogSource : public AP_HAL::AnalogSource {
public:
    AnalogSource(Zephyr::AnalogIn *parent, int16_t pin);

    float read_average()              override;
    float read_latest()               override;
    bool  set_pin(uint8_t pin)        override;
    float voltage_average()           override;
    float voltage_latest()            override;
    float voltage_average_ratiometric() override;

    int16_t pin() const { return _pin; }

private:
    static constexpr uint32_t SAMPLE_INTERVAL_MS = 10;

    Zephyr::AnalogIn *_parent;
    int16_t  _pin;
    float    _cached_v;
    uint32_t _last_sample_ms;
    int16_t  _raw_buf;
    bool     _configured;

    void _refresh();
};

class Zephyr::AnalogIn : public AP_HAL::AnalogIn {
public:
    AnalogIn();

    void init()                              override;
    AP_HAL::AnalogSource *channel(int16_t pin) override;
    float board_voltage()                    override;

    bool sample_channel(uint8_t channel_id, int16_t *raw_out);
    int32_t vref_mv() const { return _vref_mv; }

private:
#ifndef CONFIG_AP_HAL_ADC_BITS
#define CONFIG_AP_HAL_ADC_BITS 12
#endif
#ifndef CONFIG_AP_HAL_BOARD_VOLTAGE_MV
#define CONFIG_AP_HAL_BOARD_VOLTAGE_MV 3300
#endif

    static constexpr uint8_t  MAX_CHANNELS    = 16;
    static constexpr uint32_t ADC_RESOLUTION  = CONFIG_AP_HAL_ADC_BITS;
    static constexpr int32_t  DEFAULT_VREF_MV = CONFIG_AP_HAL_BOARD_VOLTAGE_MV;

    const struct device *_device;
    int32_t              _vref_mv;
    uint16_t             _channel_configured_mask;

    std::array<AnalogSource *, MAX_CHANNELS> _channels;
};
