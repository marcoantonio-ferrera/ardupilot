#pragma once

#include <zephyr/kernel.h>

// kernel_structs.h leaks these internal macros which collide with identifiers
// used in the wider ArduPilot tree; kill them here since every TU that reaches
// AP_HAL_Zephyr pulls in this header.
#ifdef _current
#undef _current
#endif
#ifdef _current_cpu
#undef _current_cpu
#endif

#include <AP_HAL/Semaphores.h>
#include "AP_HAL_Zephyr_Namespace.h"

class Zephyr::Semaphore : public AP_HAL::Semaphore {
public:
    Semaphore();

    bool take(uint32_t timeout_ms) override;
    bool take_nonblocking() override;
    bool give() override;

private:
    struct k_mutex _mutex;
};

class Zephyr::BinarySemaphore : public AP_HAL::BinarySemaphore {
public:
    BinarySemaphore(bool initial_state = false);

    bool wait(uint32_t timeout_us) override;
    bool wait_blocking() override;
    void signal() override;

private:
    struct k_sem _sem;
};
