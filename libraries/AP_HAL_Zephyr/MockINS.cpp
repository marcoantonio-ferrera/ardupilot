#include "MockINS.h"

#ifdef CONFIG_AP_HAL_ZEPHYR_MOCK_SENSORS

#include <AP_HAL/AP_HAL.h>

// BUS_TYPE_UNKNOWN so the mock can't collide with any real sensor ID
static constexpr uint32_t MOCK_GYRO_ID  =
    AP_HAL::Device::make_bus_id(AP_HAL::Device::BUS_TYPE_UNKNOWN, 0, 0x01, DEVTYPE_GYR_MPU9250);
static constexpr uint32_t MOCK_ACCEL_ID =
    AP_HAL::Device::make_bus_id(AP_HAL::Device::BUS_TYPE_UNKNOWN, 0, 0x01, DEVTYPE_ACC_MPU9250);

AP_InertialSensor_Zephyr_Mock::AP_InertialSensor_Zephyr_Mock(AP_InertialSensor &ins)
    : AP_InertialSensor_Backend(ins)
    , _accel_instance(0)
    , _gyro_instance(0)
{
}

AP_InertialSensor_Backend *AP_InertialSensor_Zephyr_Mock::probe(AP_InertialSensor &ins)
{
    AP_InertialSensor_Zephyr_Mock *backend =
        NEW_NOTHROW AP_InertialSensor_Zephyr_Mock(ins);
    if (!backend) {
        return nullptr;
    }

    // 1 kHz to match MPU-9250 default
    if (!ins.register_gyro(backend->_gyro_instance, 1000, MOCK_GYRO_ID) ||
        !ins.register_accel(backend->_accel_instance, 1000, MOCK_ACCEL_ID)) {
        delete backend;
        return nullptr;
    }

    return backend;
}

bool AP_InertialSensor_Zephyr_Mock::update()
{
    _publish_accel(_accel_instance, Vector3f(0.0f, 0.0f, -GRAVITY_MSS));
    _publish_gyro(_gyro_instance, Vector3f(0.0f, 0.0f, 0.0f));
    return true;
}

#endif
