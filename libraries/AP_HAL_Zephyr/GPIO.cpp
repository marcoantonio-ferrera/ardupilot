#include "GPIO.h"

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>

using namespace Zephyr;

GPIO::GPIO() : _dev{}, _out_dir{}, _out_val{}
{
}

void GPIO::init()
{
#if DT_HAS_ALIAS(ardupilot_gpio0) && (CONFIG_AP_HAL_GPIO_NUM_CONTROLLERS > 0)
#  if DT_NODE_HAS_STATUS(DT_ALIAS(ardupilot_gpio0), okay)
    _dev[0] = DEVICE_DT_GET(DT_ALIAS(ardupilot_gpio0));
    if (!device_is_ready(_dev[0])) { _dev[0] = nullptr; }
#  endif
#endif

#if DT_HAS_ALIAS(ardupilot_gpio1) && (CONFIG_AP_HAL_GPIO_NUM_CONTROLLERS > 1)
#  if DT_NODE_HAS_STATUS(DT_ALIAS(ardupilot_gpio1), okay)
    _dev[1] = DEVICE_DT_GET(DT_ALIAS(ardupilot_gpio1));
    if (!device_is_ready(_dev[1])) { _dev[1] = nullptr; }
#  endif
#endif

#if DT_HAS_ALIAS(ardupilot_gpio2) && (CONFIG_AP_HAL_GPIO_NUM_CONTROLLERS > 2)
#  if DT_NODE_HAS_STATUS(DT_ALIAS(ardupilot_gpio2), okay)
    _dev[2] = DEVICE_DT_GET(DT_ALIAS(ardupilot_gpio2));
    if (!device_is_ready(_dev[2])) { _dev[2] = nullptr; }
#  endif
#endif

#if DT_HAS_ALIAS(ardupilot_gpio3) && (CONFIG_AP_HAL_GPIO_NUM_CONTROLLERS > 3)
#  if DT_NODE_HAS_STATUS(DT_ALIAS(ardupilot_gpio3), okay)
    _dev[3] = DEVICE_DT_GET(DT_ALIAS(ardupilot_gpio3));
    if (!device_is_ready(_dev[3])) { _dev[3] = nullptr; }
#  endif
#endif

#if DT_HAS_ALIAS(ardupilot_gpio4) && (CONFIG_AP_HAL_GPIO_NUM_CONTROLLERS > 4)
#  if DT_NODE_HAS_STATUS(DT_ALIAS(ardupilot_gpio4), okay)
    _dev[4] = DEVICE_DT_GET(DT_ALIAS(ardupilot_gpio4));
    if (!device_is_ready(_dev[4])) { _dev[4] = nullptr; }
#  endif
#endif
}

bool GPIO::_lookup(uint8_t pin,
                   const struct device **dev,
                   uint32_t            *gpio_pin) const
{
    const uint8_t ctrl = pin / PINS_PER_CTRL;
    if (ctrl >= NUM_CTRL || _dev[ctrl] == nullptr) {
        return false;
    }
    *dev      = _dev[ctrl];
    *gpio_pin = static_cast<uint32_t>(pin % PINS_PER_CTRL);
    return true;
}

void GPIO::pinMode(uint8_t pin, uint8_t output)
{
    const struct device *dev;
    uint32_t gpio_pin;
    if (!_lookup(pin, &dev, &gpio_pin)) {
        return;
    }

    const uint8_t ctrl = pin / PINS_PER_CTRL;
    const uint32_t mask = 1u << (pin % PINS_PER_CTRL);
    if (output == HAL_GPIO_OUTPUT) {
        _out_dir[ctrl] |=  mask;
        _out_val[ctrl] &= ~mask;
    } else {
        _out_dir[ctrl] &= ~mask;
        _out_val[ctrl] &= ~mask;
    }

    const gpio_flags_t flags = (output == HAL_GPIO_OUTPUT)
                               ? GPIO_OUTPUT_INACTIVE
                               : GPIO_INPUT;
    gpio_pin_configure(dev, static_cast<gpio_pin_t>(gpio_pin), flags);
}

uint8_t GPIO::read(uint8_t pin)
{
    const struct device *dev;
    uint32_t gpio_pin;
    if (!_lookup(pin, &dev, &gpio_pin)) {
        return 0u;
    }
    const uint8_t ctrl = pin / PINS_PER_CTRL;
    const uint32_t mask = 1u << (pin % PINS_PER_CTRL);
    if (_out_dir[ctrl] & mask) {
        return (_out_val[ctrl] & mask) ? 1u : 0u;
    }
    const int val = gpio_pin_get(dev, static_cast<gpio_pin_t>(gpio_pin));
    return (val > 0) ? 1u : 0u;
}

void GPIO::write(uint8_t pin, uint8_t value)
{
    const struct device *dev;
    uint32_t gpio_pin;
    if (!_lookup(pin, &dev, &gpio_pin)) {
        return;
    }
    const uint8_t ctrl = pin / PINS_PER_CTRL;
    const uint32_t mask = 1u << (pin % PINS_PER_CTRL);
    if (value) {
        _out_val[ctrl] |=  mask;
    } else {
        _out_val[ctrl] &= ~mask;
    }
    gpio_pin_set(dev, static_cast<gpio_pin_t>(gpio_pin), value ? 1 : 0);
}

void GPIO::toggle(uint8_t pin)
{
    const struct device *dev;
    uint32_t gpio_pin;
    if (!_lookup(pin, &dev, &gpio_pin)) {
        return;
    }
    const uint8_t ctrl = pin / PINS_PER_CTRL;
    const uint32_t mask = 1u << (pin % PINS_PER_CTRL);
    _out_val[ctrl] ^= mask;
    gpio_pin_toggle(dev, static_cast<gpio_pin_t>(gpio_pin));
}

AP_HAL::DigitalSource *GPIO::channel(uint16_t n)
{
    const struct device *dev;
    uint32_t gpio_pin;
    const uint8_t pin = static_cast<uint8_t>(n & 0xFFu);
    if (!_lookup(pin, &dev, &gpio_pin)) {
        return nullptr;
    }
    const uint8_t  ctrl = pin / PINS_PER_CTRL;
    const uint32_t mask = 1u << (pin % PINS_PER_CTRL);
    return NEW_NOTHROW DigitalSource(dev, gpio_pin, &_out_val[ctrl], mask);
}

bool GPIO::usb_connected()
{
    return false;
}

DigitalSource::DigitalSource(const struct device *dev, uint32_t pin,
                             uint32_t *shared_out_val, uint32_t mask)
    : _dev(dev), _pin(pin),
      _shared_out_val(shared_out_val), _mask(mask),
      _is_output(true)
{
}

void DigitalSource::mode(uint8_t output)
{
    if (_dev == nullptr) {
        return;
    }
    _is_output = (output == HAL_GPIO_OUTPUT);
    if (_shared_out_val != nullptr) {
        *_shared_out_val &= ~_mask;
    }
    const gpio_flags_t flags = _is_output ? GPIO_OUTPUT_INACTIVE : GPIO_INPUT;
    gpio_pin_configure(_dev, static_cast<gpio_pin_t>(_pin), flags);
}

uint8_t DigitalSource::read()
{
    if (_dev == nullptr) {
        return 0u;
    }
    if (_is_output && _shared_out_val != nullptr) {
        return (*_shared_out_val & _mask) ? 1u : 0u;
    }
    const int val = gpio_pin_get(_dev, static_cast<gpio_pin_t>(_pin));
    return (val > 0) ? 1u : 0u;
}

void DigitalSource::write(uint8_t value)
{
    if (_dev == nullptr) {
        return;
    }
    if (_shared_out_val != nullptr) {
        if (value) { *_shared_out_val |=  _mask; }
        else        { *_shared_out_val &= ~_mask; }
    }
    gpio_pin_set(_dev, static_cast<gpio_pin_t>(_pin), value ? 1 : 0);
}

void DigitalSource::toggle()
{
    if (_dev == nullptr) {
        return;
    }
    if (_shared_out_val != nullptr) {
        *_shared_out_val ^= _mask;
    }
    gpio_pin_toggle(_dev, static_cast<gpio_pin_t>(_pin));
}
