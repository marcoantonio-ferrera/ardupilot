#pragma once

#define HAL_BOARD_NAME "Zephyr"
#define HAL_CPU_CLASS HAL_CPU_CLASS_1000
#define HAL_MEM_CLASS HAL_MEM_CLASS_1000

#ifndef HAL_STORAGE_SIZE
#define HAL_STORAGE_SIZE 16384
#endif

#define HAL_STORAGE_SIZE_AVAILABLE HAL_STORAGE_SIZE
#define HAL_INS_DEFAULT HAL_INS_NONE
#define CONFIG_HAL_BOARD_SUBTYPE HAL_BOARD_SUBTYPE_NONE

#ifndef HAL_PROGRAM_SIZE_LIMIT_KB
#define HAL_PROGRAM_SIZE_LIMIT_KB 4096
#endif

#ifndef HAL_BOARD_LOG_DIRECTORY
#define HAL_BOARD_LOG_DIRECTORY "/ardupilot/logs"
#endif

#ifndef HAL_BOARD_TERRAIN_DIRECTORY
#define HAL_BOARD_TERRAIN_DIRECTORY "/ardupilot/terrain"
#endif

#ifndef HAL_BOARD_STORAGE_DIRECTORY
#define HAL_BOARD_STORAGE_DIRECTORY "/ardupilot"
#endif

#ifndef HAL_LOGGING_ENABLED
#define HAL_LOGGING_ENABLED 0
#endif

#ifndef HAL_GCS_ENABLED
#define HAL_GCS_ENABLED 0
#endif

#ifndef HAL_OS_POSIX_IO
#define HAL_OS_POSIX_IO 0
#endif

#ifndef AP_SCRIPTING_ENABLED
#define AP_SCRIPTING_ENABLED 0
#endif

#ifndef HAL_OS_SOCKETS
#define HAL_OS_SOCKETS 0
#endif

#define HAL_HAVE_BOARD_VOLTAGE 0
#define HAL_HAVE_SERVO_VOLTAGE 0
#define HAL_HAVE_SAFETY_SWITCH 0
#define HAL_NUM_CAN_IFACES 0
#define HAL_WITH_DSP 0
#define HAL_USE_WSPI_DEFAULT_CFG 0

// Number of real UART ports mapped via DTS aliases.
// serial0  = zephyr,console
// serial1-4 = ardupilot-serial1 … ardupilot-serial4
// Ports beyond this use Empty::UARTDriver (zero BSS, safe no-op).
#ifndef HAL_ZEPHYR_NUM_SERIAL_PORTS
#define HAL_ZEPHYR_NUM_SERIAL_PORTS 5
#endif

#ifdef __cplusplus
#include <AP_HAL_Zephyr/Semaphores.h>
#define HAL_Semaphore Zephyr::Semaphore
#define HAL_BinarySemaphore Zephyr::BinarySemaphore
#endif
