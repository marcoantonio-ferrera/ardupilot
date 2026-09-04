// I2C0: 0x68 MPU-9250, 0x76 BMP280, 0x0c AK8963 (via MPU-9250 bypass).
#pragma once

// empty token — SPIDevice.cpp always expands HAL_ZEPHYR_SPI_DEVICES
#define HAL_ZEPHYR_SPI_DEVICES

#ifdef CONFIG_AP_HAL_ZEPHYR_MOCK_SENSORS

#ifdef __cplusplus
#include <AP_HAL_Zephyr/MockINS.h>
#include <AP_Baro/AP_Baro_Dummy.h>
#endif

#define HAL_INS_PROBE_LIST \
    ADD_BACKEND(AP_InertialSensor_Zephyr_Mock::probe(*this))
#define HAL_BARO_PROBE_LIST \
    ADD_BACKEND(AP_Baro_Dummy::probe(*this))

#else  // real hardware

#define HAL_INS_PROBE_LIST \
    ADD_BACKEND(AP_InertialSensor_Invensense::probe(*this, \
                hal.i2c_mgr->get_device(0, 0x68, 500000), \
                ROTATION_NONE))

#define HAL_BARO_PROBE_LIST \
    { probe_dev(AP_Baro_BMP280::probe, \
                hal.i2c_mgr->get_device_ptr(0, 0x76, 100000)); \
      RETURN_IF_NO_SPACE; }

#endif

// HAL_GYROFFT must be off before the AP_Math pre-include below, otherwise
// AP_Vehicle → AP_GyroFFT.h fails to find FrequencyPeak (HAL_WITH_DSP=0).
#ifndef HAL_GCS_ENABLED
#define HAL_GCS_ENABLED 1
#endif
#ifndef HAL_GYROFFT_ENABLED
#define HAL_GYROFFT_ENABLED 0
#endif
// FTP is off by default on low-mem boards; turn it back on so MP can pull param.pck.
#ifndef AP_MAVLINK_FTP_ENABLED
#define AP_MAVLINK_FTP_ENABLED 1
#endif
#ifndef AP_MATH_ALLOW_DOUBLE_FUNCTIONS
#define AP_MATH_ALLOW_DOUBLE_FUNCTIONS 1
#endif

#ifndef AP_SIM_ENABLED
#define AP_SIM_ENABLED 0
#endif

#ifndef HAL_WITH_ESC_TELEM
#define HAL_WITH_ESC_TELEM 1
#endif

#undef ARRAY_SIZE
#define ARRAY_SIZE(_arr) (sizeof(_arr) / sizeof(_arr[0]))

#include <zephyr/sys/cbprintf.h>

#ifdef __cplusplus
#include <AP_InternalError/AP_InternalError.h>
#include <AP_Math/AP_Math.h>
#include <AP_Mission/AP_Mission.h>
#endif

#undef ARRAY_SIZE
#define ARRAY_SIZE(array) (sizeof(array) / sizeof((array)[0]))

// default stream rates sized for a 57600/64 kbps SiK link
#ifndef AP_MAV_DEFAULT_STREAM_RATE_RAW_SENS
#define AP_MAV_DEFAULT_STREAM_RATE_RAW_SENS  2
#define AP_MAV_DEFAULT_STREAM_RATE_EXT_STAT  2
#define AP_MAV_DEFAULT_STREAM_RATE_RC_CHAN   2
#define AP_MAV_DEFAULT_STREAM_RATE_RAW_CTRL  0
#define AP_MAV_DEFAULT_STREAM_RATE_POSITION  3
#define AP_MAV_DEFAULT_STREAM_RATE_EXTRA1    4
#define AP_MAV_DEFAULT_STREAM_RATE_EXTRA2    2
#define AP_MAV_DEFAULT_STREAM_RATE_EXTRA3    2
#define AP_MAV_DEFAULT_STREAM_RATE_PARAMS   50
#define AP_MAV_DEFAULT_STREAM_RATE_ADSB      0
#endif

// force STREAM_PARAMS to 50 Hz — per-vehicle defaults are skipped by the
// force-include above, and rate=0 would fall back to a 100 ms interval
// that bottlenecks param download and trips MP's timeout.
#undef  AP_MAV_DEFAULT_STREAM_RATE_PARAMS
#define AP_MAV_DEFAULT_STREAM_RATE_PARAMS 50

// Serial map:
//   SERIAL0 = uart2  MAVLink2 @ 115200 (shared with Zephyr console)
//   SERIAL3 = uart3  spare
//   SERIAL4 = uart4  GPS (u-blox NEO-M8N @ 9600)
#ifndef DEFAULT_SERIAL0_PROTOCOL
#define DEFAULT_SERIAL0_PROTOCOL SerialProtocol_MAVLink2
#endif
#ifndef DEFAULT_SERIAL0_BAUD
#define DEFAULT_SERIAL0_BAUD 115
#endif

#ifndef DEFAULT_SERIAL4_PROTOCOL
#define DEFAULT_SERIAL4_PROTOCOL SerialProtocol_GPS
#endif
#ifndef DEFAULT_SERIAL4_BAUD
#define DEFAULT_SERIAL4_BAUD 9
#endif

// SBF uses asprintf (GNU-only), absent from picolibc
#ifndef AP_GPS_SBF_ENABLED
#define AP_GPS_SBF_ENABLED 0
#endif
#ifndef AP_GPS_UBLOX_ENABLED
#define AP_GPS_UBLOX_ENABLED 1
#endif

// upstream Mount_Siyi / Winch_Daiwa both miss includes — disable until fixed
#ifndef HAL_MOUNT_ENABLED
#define HAL_MOUNT_ENABLED 0
#endif

#ifndef AP_WINCH_ENABLED
#define AP_WINCH_ENABLED 0
#endif

// no I2C LEDs on this board — keep the GPIO one only
#ifndef AP_NOTIFY_TOSHIBALED_ENABLED
#define AP_NOTIFY_TOSHIBALED_ENABLED 0
#endif
#ifndef AP_NOTIFY_NCP5623_ENABLED
#define AP_NOTIFY_NCP5623_ENABLED 0
#endif
#ifndef AP_NOTIFY_LP5562_ENABLED
#define AP_NOTIFY_LP5562_ENABLED 0
#endif
#ifndef AP_NOTIFY_IS31FL3195_ENABLED
#define AP_NOTIFY_IS31FL3195_ENABLED 0
#endif
#ifndef DEFAULT_NTF_LED_TYPES
#define DEFAULT_NTF_LED_TYPES 1
#endif

// AP_BoardLED2 drives red/green on gpio2[4..5] (HAL pins 68/69, active-high)
#ifndef AP_NOTIFY_GPIO_LED_2_ENABLED
#define AP_NOTIFY_GPIO_LED_2_ENABLED 1
#endif
#ifndef HAL_GPIO_A_LED_PIN
#define HAL_GPIO_A_LED_PIN 68
#endif
#ifndef HAL_GPIO_B_LED_PIN
#define HAL_GPIO_B_LED_PIN 69
#endif
#ifndef HAL_GPIO_LED_ON
#define HAL_GPIO_LED_ON 1
#endif

#define HAL_MAG_PROBE_LIST \
    { add_backend(DRIVER_AK8963, \
                  AP_Compass_AK8963::probe_mpu9250( \
                      hal.i2c_mgr->get_device(0, 0x0c, 100000), \
                      ROTATION_NONE)); \
      RETURN_IF_NO_SPACE; }

#ifndef AP_FILESYSTEM_POSIX_ENABLED
#define AP_FILESYSTEM_POSIX_ENABLED 1
#endif
#ifndef AP_FILESYSTEM_POSIX_HAVE_STATFS
#define AP_FILESYSTEM_POSIX_HAVE_STATFS 1
#endif
#ifndef AP_FILESYSTEM_POSIX_HAVE_UTIME
#define AP_FILESYSTEM_POSIX_HAVE_UTIME 0
#endif
#ifndef HAL_BOARD_LOG_DIRECTORY
#define HAL_BOARD_LOG_DIRECTORY "/lfs/APM/logs"
#endif
#ifndef HAL_LOGGING_FILESYSTEM_ENABLED
#define HAL_LOGGING_FILESYSTEM_ENABLED 1
#endif
