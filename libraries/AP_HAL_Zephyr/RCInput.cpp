#include "RCInput.h"

#include <algorithm>
#include <cstring>

#include <AP_RCProtocol/AP_RCProtocol.h>

#ifdef CONFIG_UART_INTERRUPT_DRIVEN
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/irq.h>
#endif

using namespace Zephyr;

// C++14 ODR definition for the static constexpr passed to std::min
constexpr uint8_t RCInput::MAX_CH;

extern const AP_HAL::HAL &hal;

RCInput::RCInput()
    : _dev(nullptr)
    , _has_uart(false)
    , _values{}
    , _num_channels(0u)
    , _new_input(false)
{
}

void RCInput::init()
{
    _num_channels = 0u;
    _new_input    = false;

    AP::RC().init();

    hal.scheduler->register_timer_process(
        FUNCTOR_BIND_MEMBER(&RCInput::_timer_tick, void));

#ifdef CONFIG_UART_INTERRUPT_DRIVEN
#if DT_HAS_ALIAS(ardupilot_rcinput) && DT_NODE_HAS_STATUS(DT_ALIAS(ardupilot_rcinput), okay)
    _dev = DEVICE_DT_GET(DT_ALIAS(ardupilot_rcinput));
    if (!device_is_ready(_dev)) {
        _dev      = nullptr;
        _has_uart = false;
        return;
    }

    // SBUS is 100k 8E2; inversion has to be handled by the receiver or FPGA
    const struct uart_config sbus_cfg = {
        .baudrate  = SBUS_BAUD,
        .parity    = UART_CFG_PARITY_EVEN,
        .stop_bits = UART_CFG_STOP_BITS_2,
        .data_bits = UART_CFG_DATA_BITS_8,
        .flow_ctrl = UART_CFG_FLOW_CTRL_NONE,
    };
    if (uart_configure(_dev, &sbus_cfg) != 0) {
        // driver may not support runtime config — rely on DTS setting it
    }

    uart_irq_callback_user_data_set(_dev, _uart_isr, this);
    uart_irq_rx_enable(_dev);
    _has_uart = true;
#endif
#endif
}

#ifdef CONFIG_UART_INTERRUPT_DRIVEN
void RCInput::_uart_isr(const struct device *dev, void *user_data)
{
    (void)user_data;
    while (uart_irq_update(dev) && uart_irq_rx_ready(dev)) {
        uint8_t byte;
        if (uart_fifo_read(dev, &byte, 1) == 1) {
            AP::RC().process_byte(byte, SBUS_BAUD);
        }
    }
}
#endif

void RCInput::_timer_tick()
{
    if (!AP::RC().new_input()) {
        return;
    }
    unsigned int key = irq_lock();
    _num_channels = std::min<uint8_t>(AP::RC().num_channels(), MAX_CH);
    AP::RC().read(_values, _num_channels);
    _new_input = true;
    irq_unlock(key);
}

bool RCInput::new_input()
{
    if (!_new_input) {
        return false;
    }
    unsigned int key = irq_lock();
    _new_input = false;
    irq_unlock(key);
    return true;
}

uint8_t RCInput::num_channels()
{
    return _num_channels;
}

uint16_t RCInput::read(uint8_t ch)
{
    if (ch >= _num_channels) {
        return 1500u;
    }
    unsigned int key = irq_lock();
    const uint16_t val = _values[ch];
    irq_unlock(key);
    return val;
}

uint8_t RCInput::read(uint16_t *periods, uint8_t len)
{
    const uint8_t count = std::min<uint8_t>(len, _num_channels);
    if (count == 0u) {
        return 0u;
    }
    unsigned int key = irq_lock();
    memcpy(periods, _values, count * sizeof(uint16_t));
    irq_unlock(key);
    return count;
}

const char *RCInput::protocol() const
{
    if (!_has_uart) {
        return "ZephyrStub";
    }
    const char *name = AP::RC().detected_protocol_name();
    return name ? name : "searching";
}
