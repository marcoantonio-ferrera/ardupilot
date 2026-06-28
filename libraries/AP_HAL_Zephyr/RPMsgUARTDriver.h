#pragma once

#include <AP_HAL/AP_HAL.h>
#include "AP_HAL_Zephyr.h"

/* Backed by the OpenAMP rpmsg-tty endpoint, carrying AP_DDS to the Linux
 * master. Unlike Zephyr::UARTDriver it has no devicetree device or IRQ —
 * buffering/flow-control lives in the rpmsg bridge, not here. */

#ifndef HAL_ZEPHYR_RPMSG_SERIAL_ENABLED
#define HAL_ZEPHYR_RPMSG_SERIAL_ENABLED 0
#endif

class Zephyr::RPMsgUARTDriver : public AP_HAL::UARTDriver {
public:
    RPMsgUARTDriver();

    bool is_initialized() override;
    bool tx_pending() override;
    uint32_t txspace() override;
    uint32_t get_baud_rate() const override { return 0; }
    uint32_t bw_in_bytes_per_second() const override { return 1000000; }
    enum flow_control get_flow_control(void) override { return FLOW_CONTROL_DISABLE; }

protected:
    void _begin(uint32_t baud, uint16_t rxSpace, uint16_t txSpace) override;
    void _end() override;
    void _flush() override;
    uint32_t _available() override;
    ssize_t _read(uint8_t *buffer, uint16_t count) override;
    size_t _write(const uint8_t *buffer, size_t size) override;
    bool _discard_input() override;

private:
    bool _initialized;
};
