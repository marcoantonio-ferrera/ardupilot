#pragma once
// Zero-data IMU backend for Renode — static 1 g, no rotation.
// Selected by CONFIG_AP_HAL_ZEPHYR_MOCK_SENSORS; sim-only.

#ifdef CONFIG_AP_HAL_ZEPHYR_MOCK_SENSORS

#include <AP_InertialSensor/AP_InertialSensor.h>
#include <AP_InertialSensor/AP_InertialSensor_Backend.h>

class AP_InertialSensor_Zephyr_Mock : public AP_InertialSensor_Backend
{
public:
    explicit AP_InertialSensor_Zephyr_Mock(AP_InertialSensor &ins);

    static AP_InertialSensor_Backend *probe(AP_InertialSensor &ins);

    bool update() override;
    void accumulate() override {}

private:
    uint8_t _accel_instance;
    uint8_t _gyro_instance;
};

#endif
