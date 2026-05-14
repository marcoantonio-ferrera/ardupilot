#pragma once

#include <AP_HAL/AP_HAL.h>
#include "AP_HAL_Zephyr_Namespace.h"

struct device;

class Zephyr::GPIO : public AP_HAL::GPIO {
public:
    GPIO();

    void init() override;
    void pinMode(uint8_t pin, uint8_t output) override;
    uint8_t read(uint8_t pin) override;
    void write(uint8_t pin, uint8_t value) override;
    void toggle(uint8_t pin) override;
    AP_HAL::DigitalSource *channel(uint16_t n) override;
    bool usb_connected() override;

private:
#ifndef CONFIG_AP_HAL_GPIO_NUM_CONTROLLERS
#define CONFIG_AP_HAL_GPIO_NUM_CONTROLLERS 3
#endif
    static constexpr uint8_t NUM_CTRL      = CONFIG_AP_HAL_GPIO_NUM_CONTROLLERS;
    static constexpr uint8_t PINS_PER_CTRL = 32u;

    const struct device *_dev[NUM_CTRL];

    // shadow output state so read() works on driven pins under sim models
    // that don't loop the output register back to the input.
    uint32_t _out_dir[NUM_CTRL];
    uint32_t _out_val[NUM_CTRL];

    bool _lookup(uint8_t pin,
                 const struct device **dev,
                 uint32_t            *gpio_pin) const;
};

class Zephyr::DigitalSource : public AP_HAL::DigitalSource {
public:
    DigitalSource(const struct device *dev, uint32_t pin,
                  uint32_t *shared_out_val, uint32_t mask);

    void    mode(uint8_t output) override;
    uint8_t read() override;
    void    write(uint8_t value) override;
    void    toggle() override;

private:
    const struct device *_dev;
    uint32_t             _pin;
    uint32_t            *_shared_out_val;
    uint32_t             _mask;
    bool                 _is_output;
};
