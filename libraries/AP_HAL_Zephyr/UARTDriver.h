#pragma once

#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/irq.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/ring_buffer.h>

#include <AP_HAL/AP_HAL.h>
#include "AP_HAL_Zephyr.h"

class Zephyr::UARTDriver : public AP_HAL::UARTDriver {
public:
    explicit UARTDriver(uint8_t serial_index);

    bool is_initialized() override;
    bool tx_pending() override;
    uint32_t txspace() override;
    uint32_t get_baud_rate() const override;
    uint32_t bw_in_bytes_per_second() const override;
    enum flow_control get_flow_control(void) override;

protected:
    void _begin(uint32_t baud, uint16_t rxSpace, uint16_t txSpace) override;
    void _end() override;
    void _flush() override;
    uint32_t _available() override;
    ssize_t _read(uint8_t *buffer, uint16_t count) override;
    size_t _write(const uint8_t *buffer, size_t size) override;
    bool _discard_input() override;

private:
    static constexpr uint16_t RX_BUF_SIZE  = 2048;
#ifndef CONFIG_AP_HAL_UART_TX_BUF_SIZE
#define CONFIG_AP_HAL_UART_TX_BUF_SIZE 32768
#endif
    static constexpr uint32_t TX_BUF_SIZE  = CONFIG_AP_HAL_UART_TX_BUF_SIZE;

    const struct device *_device;
    uint32_t  _baudrate;
    bool      _initialized;
    uint8_t   _serial_index;

    uint8_t         _rx_buf_data[RX_BUF_SIZE];
    struct ring_buf _rx_ring;

    uint8_t         _tx_buf_data[TX_BUF_SIZE];
    struct ring_buf _tx_ring;

#ifdef CONFIG_AP_HAL_UART_PLIC_REARM
    uint32_t _irq_num;
    bool     _tx_watchdog_registered;
#endif

    static void _uart_irq_cb(const struct device *dev, void *user_data);
    void _irq_handler();
#ifdef CONFIG_AP_HAL_UART_PLIC_REARM
    void _tx_watchdog();
#endif

    void _resolve_device();
};
