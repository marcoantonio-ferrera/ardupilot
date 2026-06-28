#include "RPMsgUARTDriver.h"

#if HAL_ZEPHYR_RPMSG_SERIAL_ENABLED

#include <zephyr/kernel.h>
#include "ipc/ap_rpmsg_serial.h"

using namespace Zephyr;

RPMsgUARTDriver::RPMsgUARTDriver() :
    _initialized(false)
{
}

// _initialized is set unconditionally so SerialManager binds the protocol at
// boot even though the rpmsg link itself comes up asynchronously later.
void RPMsgUARTDriver::_begin(uint32_t baud, uint16_t rxSpace, uint16_t txSpace)
{
    (void)baud;
    (void)rxSpace;
    (void)txSpace;
    _initialized = true;
}

void RPMsgUARTDriver::_end()
{
    _initialized = false;
}

void RPMsgUARTDriver::_flush()
{
    // mirrors UARTDriver::_flush's bounded drain wait
    for (int i = 0; i < 100 && ap_rpmsg_serial_tx_pending(); i++) {
        k_msleep(1);
    }
}

bool RPMsgUARTDriver::is_initialized()
{
    return _initialized;
}

bool RPMsgUARTDriver::tx_pending()
{
    return ap_rpmsg_serial_tx_pending();
}

uint32_t RPMsgUARTDriver::txspace()
{
    if (!_initialized) {
        return 0;
    }
    return ap_rpmsg_serial_txspace();
}

uint32_t RPMsgUARTDriver::_available()
{
    if (!_initialized) {
        return 0;
    }
    return ap_rpmsg_serial_rxavail();
}

ssize_t RPMsgUARTDriver::_read(uint8_t *buffer, uint16_t count)
{
    if (!_initialized || buffer == nullptr || count == 0) {
        return 0;
    }
    return (ssize_t)ap_rpmsg_serial_read(buffer, count);
}

size_t RPMsgUARTDriver::_write(const uint8_t *buffer, size_t size)
{
    if (!_initialized || buffer == nullptr || size == 0) {
        return 0;
    }
    return (size_t)ap_rpmsg_serial_write(buffer, (uint32_t)size);
}

bool RPMsgUARTDriver::_discard_input()
{
    ap_rpmsg_serial_discard_rx();
    return true;
}

#endif
