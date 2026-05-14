#pragma once

#include <AP_HAL/AP_HAL.h>

#include "AP_HAL_Zephyr_Namespace.h"

class HAL_Zephyr : public AP_HAL::HAL {
public:
    HAL_Zephyr();
    void run(int argc, char *const *argv, Callbacks *callbacks) const override;
};
