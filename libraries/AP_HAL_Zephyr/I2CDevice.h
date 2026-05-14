#pragma once

#include <AP_HAL/AP_HAL.h>
#include <AP_HAL/I2CDevice.h>
#include "AP_HAL_Zephyr_Namespace.h"

struct device;
#include "Semaphores.h"

class Zephyr::I2CDevice : public AP_HAL::I2CDevice {
public:
    I2CDevice(const struct device *dev, uint8_t address, uint32_t bus_clock);

    void set_address(uint8_t address) override;
    void set_retries(uint8_t retries) override;
    bool transfer(const uint8_t *send, uint32_t send_len, uint8_t *recv, uint32_t recv_len) override;
    bool read_registers_multiple(uint8_t first_reg, uint8_t *recv, uint32_t recv_len, uint8_t times) override;
    bool set_speed(enum AP_HAL::Device::Speed speed) override;
    AP_HAL::Semaphore *get_semaphore() override;
    AP_HAL::Device::PeriodicHandle register_periodic_callback(uint32_t period_usec, AP_HAL::Device::PeriodicCb cb) override;
    bool adjust_periodic_callback(AP_HAL::Device::PeriodicHandle h, uint32_t period_usec) override;
    bool unregister_callback(PeriodicHandle h) override;

private:
    const struct device *_dev;
    uint8_t _address;
    uint8_t _retries;
    uint32_t _bus_clock;  // Hz
    Semaphore _semaphore;

    bool _transfer_once(const uint8_t *send, uint32_t send_len, uint8_t *recv, uint32_t recv_len);
};

class Zephyr::I2CDeviceManager : public AP_HAL::I2CDeviceManager {
public:
    AP_HAL::I2CDevice *get_device_ptr(uint8_t bus, uint8_t address, uint32_t bus_clock, bool use_smbus, uint32_t timeout_ms) override;

    uint32_t get_bus_mask(void) const override;
    uint32_t get_bus_mask_external(void) const override;
    uint32_t get_bus_mask_internal(void) const override;

    void begin();

private:
    static const struct device *_bus_device(uint8_t bus);
    static uint32_t _available_bus_mask();
};
