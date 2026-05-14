#include "I2CDevice.h"
#include "PeriodicCallback.h"

#include <zephyr/drivers/i2c.h>
#include <zephyr/irq.h>
#include <zephyr/sys/printk.h>

using namespace Zephyr;


void I2CDeviceManager::begin()
{
}

const struct device *I2CDeviceManager::_bus_device(uint8_t bus)
{
    switch (bus) {
#if DT_HAS_ALIAS(ardupilot_i2c0) && DT_NODE_HAS_STATUS(DT_ALIAS(ardupilot_i2c0), okay)
    case 0: return DEVICE_DT_GET(DT_ALIAS(ardupilot_i2c0));
#endif
#if DT_HAS_ALIAS(ardupilot_i2c1) && DT_NODE_HAS_STATUS(DT_ALIAS(ardupilot_i2c1), okay)
    case 1: return DEVICE_DT_GET(DT_ALIAS(ardupilot_i2c1));
#endif
    default: return nullptr;
    }
}

I2CDevice::I2CDevice(const struct device *dev, uint8_t address, uint32_t bus_clock) :
    _dev(dev),
    _address(address),
    _retries(0),
    _bus_clock(bus_clock)
{
}

void I2CDevice::set_address(uint8_t address)
{
    _address = address;
}

void I2CDevice::set_retries(uint8_t retries)
{
    _retries = retries;
}

#ifndef CONFIG_AP_HAL_I2C_FAST_THRESHOLD_HZ
#define CONFIG_AP_HAL_I2C_FAST_THRESHOLD_HZ 400000
#endif

bool I2CDevice::set_speed(enum AP_HAL::Device::Speed speed)
{
#ifdef CONFIG_I2C
    if (_dev == nullptr || !device_is_ready(_dev)) {
        return false;
    }
    uint32_t cfg = I2C_MODE_CONTROLLER;
    // > (threshold-1) is clock >= threshold without widening to 64-bit.
    // MPFS uses threshold=400001 so bus_clock=400000 stays on STANDARD.
    const uint32_t clock = (speed == AP_HAL::Device::SPEED_LOW) ? 100000 : _bus_clock;
    cfg |= (clock > (CONFIG_AP_HAL_I2C_FAST_THRESHOLD_HZ - 1))
           ? I2C_SPEED_SET(I2C_SPEED_FAST)
           : I2C_SPEED_SET(I2C_SPEED_STANDARD);
    return i2c_configure(_dev, cfg) == 0;
#else
    (void)speed;
    return true;
#endif
}

AP_HAL::Semaphore *I2CDevice::get_semaphore()
{
    return &_semaphore;
}

AP_HAL::Device::PeriodicHandle I2CDevice::register_periodic_callback(
    uint32_t period_usec, AP_HAL::Device::PeriodicCb cb)
{
    return AP_Zephyr_register_periodic_callback(period_usec, cb);
}

bool I2CDevice::adjust_periodic_callback(
    AP_HAL::Device::PeriodicHandle h, uint32_t period_usec)
{
    return AP_Zephyr_adjust_periodic_callback(h, period_usec);
}

bool I2CDevice::unregister_callback(AP_HAL::Device::PeriodicHandle h)
{
    return AP_Zephyr_unregister_callback(h);
}

bool I2CDevice::_transfer_once(const uint8_t *send, uint32_t send_len,
                               uint8_t *recv, uint32_t recv_len)
{
#ifdef CONFIG_I2C
    if (_dev == nullptr || !device_is_ready(_dev)) {
        return false;
    }

    struct i2c_msg msgs[2];
    uint8_t num_msgs = 0;

    if (send != nullptr && send_len > 0) {
        msgs[num_msgs].buf   = const_cast<uint8_t *>(send);
        msgs[num_msgs].len   = send_len;
        msgs[num_msgs].flags = I2C_MSG_WRITE | ((recv_len > 0) ? 0u : I2C_MSG_STOP);
        num_msgs++;
    }

    if (recv != nullptr && recv_len > 0) {
        // MPFS MSS I2C rejects a write→read turn without I2C_MSG_RESTART
#ifdef CONFIG_AP_HAL_I2C_FORCE_MSG_RESTART
        const uint32_t restart_flag = (num_msgs > 0) ? I2C_MSG_RESTART : 0u;
#else
        const uint32_t restart_flag = 0u;
#endif
        msgs[num_msgs].buf   = recv;
        msgs[num_msgs].len   = recv_len;
        msgs[num_msgs].flags = I2C_MSG_READ | I2C_MSG_STOP | restart_flag;
        num_msgs++;
    }

    if (num_msgs == 0) {
        return true;
    }

    return (i2c_transfer(_dev, msgs, num_msgs, _address) == 0);
#else
    (void)send; (void)send_len; (void)recv; (void)recv_len;
    return false;
#endif
}

bool I2CDevice::transfer(const uint8_t *send, uint32_t send_len,
                         uint8_t *recv, uint32_t recv_len)
{
    for (uint8_t attempt = 0; attempt <= _retries; attempt++) {
        if (_transfer_once(send, send_len, recv, recv_len)) {
            return true;
        }
    }
    return false;
}

bool I2CDevice::read_registers_multiple(uint8_t first_reg, uint8_t *recv,
                                        uint32_t recv_len, uint8_t times)
{
    for (uint8_t i = 0; i < times; i++) {
        if (!transfer(&first_reg, 1, recv + (uint32_t)i * recv_len, recv_len)) {
            return false;
        }
    }
    return true;
}

uint32_t I2CDeviceManager::_available_bus_mask()
{
    uint32_t mask = 0;
#if DT_HAS_ALIAS(ardupilot_i2c0) && DT_NODE_HAS_STATUS(DT_ALIAS(ardupilot_i2c0), okay)
    mask |= (1U << 0);
#endif
#if DT_HAS_ALIAS(ardupilot_i2c1) && DT_NODE_HAS_STATUS(DT_ALIAS(ardupilot_i2c1), okay)
    mask |= (1U << 1);
#endif
#if DT_HAS_ALIAS(ardupilot_i2c2) && DT_NODE_HAS_STATUS(DT_ALIAS(ardupilot_i2c2), okay)
    mask |= (1U << 2);
#endif
#if DT_HAS_ALIAS(ardupilot_i2c3) && DT_NODE_HAS_STATUS(DT_ALIAS(ardupilot_i2c3), okay)
    mask |= (1U << 3);
#endif
    return mask;
}

uint32_t I2CDeviceManager::get_bus_mask(void) const           { return _available_bus_mask(); }
uint32_t I2CDeviceManager::get_bus_mask_external(void) const  { return _available_bus_mask(); }
uint32_t I2CDeviceManager::get_bus_mask_internal(void) const  { return _available_bus_mask(); }

AP_HAL::I2CDevice *I2CDeviceManager::get_device_ptr(uint8_t bus, uint8_t address,
                                                     uint32_t bus_clock,
                                                     bool use_smbus,
                                                     uint32_t timeout_ms)
{
    (void)use_smbus;
    (void)timeout_ms;

    const struct device *dev = _bus_device(bus);
    if (dev == nullptr) {
        printk("AP_HAL_Zephyr I2C: bus %u not available\n", bus);
    }

#ifdef CONFIG_I2C
    if (dev != nullptr && device_is_ready(dev) && bus_clock > 0) {
        uint32_t cfg = I2C_MODE_CONTROLLER |
                       ((bus_clock > (CONFIG_AP_HAL_I2C_FAST_THRESHOLD_HZ - 1))
                        ? I2C_SPEED_SET(I2C_SPEED_FAST)
                        : I2C_SPEED_SET(I2C_SPEED_STANDARD));
        i2c_configure(dev, cfg);
    }
#else
    (void)bus_clock;
#endif

    return NEW_NOTHROW I2CDevice(dev, address, bus_clock);
}
