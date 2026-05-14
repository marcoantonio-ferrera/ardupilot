#pragma once

#include <zephyr/kernel.h>
#include <AP_HAL/AP_HAL.h>

// Each register_periodic_callback() allocates one of these; the delayable
// work reschedules itself to form a self-sustaining sensor ticker.
// Runs on ap_device_wq, started by Scheduler::init().

struct AP_Zephyr_PeriodicCBWork {
    struct k_work_delayable    work;
    AP_HAL::Device::PeriodicCb cb;
    uint32_t                   period_usec;
    bool                       cancelled;
};

extern struct k_work_q ap_device_wq;

AP_HAL::Device::PeriodicHandle AP_Zephyr_register_periodic_callback(
    uint32_t period_usec, AP_HAL::Device::PeriodicCb cb);

bool AP_Zephyr_adjust_periodic_callback(
    AP_HAL::Device::PeriodicHandle h, uint32_t period_usec);

bool AP_Zephyr_unregister_callback(AP_HAL::Device::PeriodicHandle h);

void AP_Zephyr_start_device_work_queue();
