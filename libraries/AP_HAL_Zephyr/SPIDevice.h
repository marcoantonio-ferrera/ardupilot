#pragma once

#include <AP_HAL/AP_HAL.h>
#include "AP_HAL_Zephyr_Namespace.h"
#include "Semaphores.h"

struct device;  // forward-declare to avoid pulling in Zephyr headers here

class Zephyr::SPIDevice : public AP_HAL::SPIDevice {
public:
    SPIDevice(const struct device *dev, uint8_t cs,
              uint32_t lowspeed, uint32_t highspeed, uint8_t mode);

    bool set_speed(AP_HAL::Device::Speed speed) override;
    bool transfer(const uint8_t *send, uint32_t send_len,
                  uint8_t *recv, uint32_t recv_len) override;
    bool transfer_fullduplex(const uint8_t *send, uint8_t *recv,
                             uint32_t len) override;
    AP_HAL::Semaphore *get_semaphore() override;
    AP_HAL::Device::PeriodicHandle register_periodic_callback(
        uint32_t period_usec, AP_HAL::Device::PeriodicCb cb) override;
    bool adjust_periodic_callback(PeriodicHandle h,
                                  uint32_t period_usec) override;
    bool unregister_callback(PeriodicHandle h) override;

private:
    const struct device *_dev;
    uint8_t  _cs;
    uint32_t _lowspeed;
    uint32_t _highspeed;
    uint8_t  _mode;
    bool     _high_speed;
    Semaphore _semaphore;
};

class Zephyr::SPIDeviceManager : public AP_HAL::SPIDeviceManager {
public:
    AP_HAL::SPIDevice *get_device_ptr(const char *name) override;

private:
    static const struct device *_bus_device(uint8_t bus);
};
