#include <AP_HAL/AP_HAL.h>
#if CONFIG_HAL_BOARD == HAL_BOARD_ZEPHYR

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include <AP_HAL_Empty/DSP.h>
#include <AP_HAL_Empty/Flash.h>
#include <AP_HAL_Empty/OpticalFlow.h>
#include <AP_HAL_Empty/UARTDriver.h>
#include <AP_HAL_Empty/WSPIDevice.h>

#include "HAL_Zephyr_Class.h"
#include "AP_HAL_Zephyr_Private.h"

using namespace Zephyr;

// ports above 4 fall back to Empty::UARTDriver so HAL_ZEPHYR_NUM_SERIAL_PORTS
// can be tuned down per-board without leaving dangling instances.
static UARTDriver serial0Driver(0);
static UARTDriver serial1Driver(1);
static UARTDriver serial2Driver(2);
static UARTDriver serial3Driver(3);
static UARTDriver serial4Driver(4);

#if HAL_ZEPHYR_NUM_SERIAL_PORTS > 5
static UARTDriver serial5Driver(5);
#else
static Empty::UARTDriver serial5Driver;
#endif

#if HAL_ZEPHYR_NUM_SERIAL_PORTS > 6
static UARTDriver serial6Driver(6);
#else
static Empty::UARTDriver serial6Driver;
#endif

#if HAL_ZEPHYR_NUM_SERIAL_PORTS > 7
static UARTDriver serial7Driver(7);
#else
static Empty::UARTDriver serial7Driver;
#endif

#if HAL_ZEPHYR_NUM_SERIAL_PORTS > 8
static UARTDriver serial8Driver(8);
#else
static Empty::UARTDriver serial8Driver;
#endif

#if HAL_ZEPHYR_NUM_SERIAL_PORTS > 9
static UARTDriver serial9Driver(9);
#else
static Empty::UARTDriver serial9Driver;
#endif

static I2CDeviceManager i2cDeviceManager;
static SPIDeviceManager spiDeviceManager;
static Empty::WSPIDeviceManager wspiDeviceManager;
static AnalogIn analogIn;
static Storage storageDriver;
static GPIO gpioDriver;
static RCInput rcinDriver;
static RCOutput rcoutDriver;
static Scheduler schedulerInstance;
static Util utilInstance;
static Empty::OpticalFlow opticalFlowDriver;
static Empty::Flash flashDriver;
static Empty::DSP dspDriver;

HAL_Zephyr::HAL_Zephyr() :
    AP_HAL::HAL(
        &serial0Driver,
        &serial1Driver,
        &serial2Driver,
        &serial3Driver,
        &serial4Driver,
        &serial5Driver,
        &serial6Driver,
        &serial7Driver,
        &serial8Driver,
        &serial9Driver,
        &i2cDeviceManager,
        &spiDeviceManager,
        &wspiDeviceManager,
        &analogIn,
        &storageDriver,
        &serial0Driver,
        &gpioDriver,
        &rcinDriver,
        &rcoutDriver,
        &schedulerInstance,
        &utilInstance,
        &opticalFlowDriver,
        &flashDriver,
#if AP_SIM_ENABLED
        nullptr,
#endif
#if HAL_WITH_DSP
        &dspDriver,
#endif
        nullptr)
{
}

void HAL_Zephyr::run(int argc, char *const argv[], Callbacks *callbacks) const
{
    schedulerInstance.set_arguments(argc, argv);
    schedulerInstance.set_callbacks(callbacks);
    scheduler->init();

    i2cDeviceManager.begin();

    serial0Driver.begin(115200);
    gpioDriver.init();
    rcinDriver.init();
    rcoutDriver.init();
    storageDriver.init();
    analogIn.init();

    callbacks->setup();

    scheduler->set_system_initialized();

    for (;;) {
        schedulerInstance.tick();
        callbacks->loop();
    }
}

#endif
