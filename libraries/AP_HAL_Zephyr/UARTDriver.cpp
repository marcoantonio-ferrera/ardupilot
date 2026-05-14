#include "UARTDriver.h"

#include <zephyr/devicetree.h>
#include <zephyr/sys/printk.h>

extern const AP_HAL::HAL& hal;

using namespace Zephyr;

UARTDriver::UARTDriver(uint8_t serial_index) :
    _device(nullptr),
    _baudrate(0),
    _initialized(false),
    _serial_index(serial_index)
#ifdef CONFIG_AP_HAL_UART_PLIC_REARM
    , _irq_num(0)
    , _tx_watchdog_registered(false)
#endif
{
}

void UARTDriver::_resolve_device()
{
    _device  = nullptr;
#ifdef CONFIG_AP_HAL_UART_PLIC_REARM
    _irq_num = 0;
#define _SET_IRQN(alias_or_chosen) _irq_num = DT_IRQN(alias_or_chosen)
#else
#define _SET_IRQN(alias_or_chosen) ((void)0)
#endif

    switch (_serial_index) {
    case 0:
        // fall back to zephyr,console if no explicit serial0 alias
#if DT_HAS_ALIAS(ardupilot_serial0)
        _device  = DEVICE_DT_GET(DT_ALIAS(ardupilot_serial0));
        _SET_IRQN(DT_ALIAS(ardupilot_serial0));
#elif DT_HAS_CHOSEN(zephyr_console)
        _device  = DEVICE_DT_GET(DT_CHOSEN(zephyr_console));
        _SET_IRQN(DT_CHOSEN(zephyr_console));
#endif
        break;
    case 1:
#if DT_HAS_ALIAS(ardupilot_serial1)
        _device  = DEVICE_DT_GET(DT_ALIAS(ardupilot_serial1));
        _SET_IRQN(DT_ALIAS(ardupilot_serial1));
#endif
        break;
    case 2:
#if DT_HAS_ALIAS(ardupilot_serial2)
        _device  = DEVICE_DT_GET(DT_ALIAS(ardupilot_serial2));
        _SET_IRQN(DT_ALIAS(ardupilot_serial2));
#endif
        break;
    case 3:
#if DT_HAS_ALIAS(ardupilot_serial3)
        _device  = DEVICE_DT_GET(DT_ALIAS(ardupilot_serial3));
        _SET_IRQN(DT_ALIAS(ardupilot_serial3));
#endif
        break;
    case 4:
#if DT_HAS_ALIAS(ardupilot_serial4)
        _device  = DEVICE_DT_GET(DT_ALIAS(ardupilot_serial4));
        _SET_IRQN(DT_ALIAS(ardupilot_serial4));
#endif
        break;
    default:
        break;
    }

#undef _SET_IRQN

    if (_device != nullptr && !device_is_ready(_device)) {
        printk("AP_HAL_Zephyr: serial%u device not ready\n",
               (unsigned)_serial_index);
        _device = nullptr;
    }
}


void UARTDriver::_uart_irq_cb(const struct device *dev, void *user_data)
{
    (void)dev;
    static_cast<UARTDriver *>(user_data)->_irq_handler();
}

void UARTDriver::_irq_handler()
{
    uart_irq_update(_device);

    while (uart_irq_rx_ready(_device)) {
        uint8_t tmp[32];
        int n = uart_fifo_read(_device, tmp, sizeof(tmp));
        if (n <= 0) {
            break;
        }
        ring_buf_put(&_rx_ring, tmp, (uint32_t)n);
    }

#ifdef CONFIG_UART_NS16550
    // NS16550 on edge-triggered PLIC: RX drain leaves THRE asserted
    // with no new edge; re-read IIR so TX isn't stuck until next RX.
    uart_irq_update(_device);
#endif

    if (uart_irq_tx_ready(_device)) {
        uint8_t *ptr;
        uint32_t available = ring_buf_get_claim(&_tx_ring, &ptr, 256);
        if (available == 0) {
            uart_irq_tx_disable(_device);
        } else {
            int sent = uart_fifo_fill(_device, ptr, (int)available);
            ring_buf_get_finish(&_tx_ring, (uint32_t)sent);
        }
    }
}

bool UARTDriver::is_initialized()
{
    return _initialized;
}

bool UARTDriver::tx_pending()
{
#ifdef CONFIG_UART_INTERRUPT_DRIVEN
    return !ring_buf_is_empty(&_tx_ring);
#else
    return false;
#endif
}

uint32_t UARTDriver::txspace()
{
#ifdef CONFIG_UART_INTERRUPT_DRIVEN
    if (!_initialized) {
        return 0;
    }
    return ring_buf_space_get(&_tx_ring);
#else
    return TX_BUF_SIZE;
#endif
}

uint32_t UARTDriver::get_baud_rate() const
{
    return _baudrate;
}

uint32_t UARTDriver::bw_in_bytes_per_second() const
{
    // tuned for MP param download: baud/6 keeps wire utilisation ~50%
    // so MAVLink telemetry can coexist without overflowing the TX ring.
    return (_baudrate != 0) ? _baudrate / 6 : 0;
}

AP_HAL::UARTDriver::flow_control UARTDriver::get_flow_control(void)
{
    // report flow control as present so GCS_Param uses the bw-paced count;
    // the DISABLE path caps at 5 params/call and blows MP's timeout.
    return AP_HAL::UARTDriver::FLOW_CONTROL_ENABLE;
}

#ifndef CONFIG_AP_HAL_UART_SERIAL0_MIN_BAUD
#define CONFIG_AP_HAL_UART_SERIAL0_MIN_BAUD 0
#endif

void UARTDriver::_begin(uint32_t baud, uint16_t rxSpace, uint16_t txSpace)
{
    (void)rxSpace;
    (void)txSpace;

    // optional floor on Serial0 baud so a stale SERIAL0_BAUD param
    // can't drop us below the link rate and lock out the GCS.
#if CONFIG_AP_HAL_UART_SERIAL0_MIN_BAUD > 0
    if (_serial_index == 0 && baud > 0 && baud < CONFIG_AP_HAL_UART_SERIAL0_MIN_BAUD) {
        baud = CONFIG_AP_HAL_UART_SERIAL0_MIN_BAUD;
    }
#endif

    _baudrate = baud;
    _resolve_device();

    if (_device == nullptr) {
        return;
    }

    ring_buf_init(&_rx_ring, RX_BUF_SIZE, _rx_buf_data);
    ring_buf_init(&_tx_ring, TX_BUF_SIZE, _tx_buf_data);

    if (_device == nullptr) {
        return;
    }

#ifdef CONFIG_UART_INTERRUPT_DRIVEN

    if (baud != 0) {
        struct uart_config cfg;
        if (uart_config_get(_device, &cfg) == 0) {
            if (cfg.baudrate != baud) {
                cfg.baudrate = baud;
                uart_configure(_device, &cfg);
            }
        }
    }

    uart_irq_callback_user_data_set(_device, _uart_irq_cb, this);
#ifdef CONFIG_AP_HAL_UART_PLIC_REARM
    if (_irq_num != 0) {
        irq_enable(_irq_num);
    }
#endif
    uart_irq_rx_enable(_device);

#ifdef CONFIG_AP_HAL_UART_PLIC_REARM
    if (!_tx_watchdog_registered) {
        hal.scheduler->register_timer_process(FUNCTOR_BIND_MEMBER(&UARTDriver::_tx_watchdog, void));
        _tx_watchdog_registered = true;
    }
#endif
#endif

    _initialized = true;
}

void UARTDriver::_end()
{
    _initialized = false;
#ifdef CONFIG_UART_INTERRUPT_DRIVEN
    if (_device != nullptr) {
        _flush();
        uart_irq_rx_disable(_device);
        uart_irq_tx_disable(_device);
    }
#endif
}

void UARTDriver::_flush()
{
#ifdef CONFIG_UART_INTERRUPT_DRIVEN
    for (int i = 0; i < 100; i++) {
        if (ring_buf_is_empty(&_tx_ring)) {
            break;
        }
        k_msleep(1);
    }
#endif
}

uint32_t UARTDriver::_available()
{
    if (!_initialized) {
        return 0;
    }
    return ring_buf_size_get(&_rx_ring);
}

ssize_t UARTDriver::_read(uint8_t *buffer, uint16_t count)
{
    if (!_initialized || buffer == nullptr || count == 0) {
        return 0;
    }
    return (ssize_t)ring_buf_get(&_rx_ring, buffer, count);
}

size_t UARTDriver::_write(const uint8_t *buffer, size_t size)
{
    if (buffer == nullptr || size == 0) {
        return 0;
    }

    if (_device == nullptr) {
        return 0;
    }

#ifdef CONFIG_UART_INTERRUPT_DRIVEN
    unsigned int key = irq_lock();
    uint32_t written = ring_buf_put(&_tx_ring, buffer, (uint32_t)size);
    irq_unlock(key);

    // !empty (not written>0) so a ring-full write still kicks TX.
    // irq_enable re-arms PLIC (OpenSBI AMP clears enables at boot).
    if (!ring_buf_is_empty(&_tx_ring)) {
#ifdef CONFIG_AP_HAL_UART_PLIC_REARM
        if (_irq_num != 0) {
            irq_enable(_irq_num);
        }
#endif
        uart_irq_tx_enable(_device);
    }

    return (size_t)written;
#else
    for (size_t i = 0; i < size; i++) {
        uart_poll_out(_device, buffer[i]);
    }
    return size;
#endif
}

#ifdef CONFIG_AP_HAL_UART_PLIC_REARM
void UARTDriver::_tx_watchdog()
{
#ifdef CONFIG_UART_INTERRUPT_DRIVEN
    if (!_initialized || _device == nullptr) {
        return;
    }

    if (!ring_buf_is_empty(&_tx_ring)) {
        // mirrors the re-arm in _write() to catch stalls during quiet
        // periods (ring full, no new write): toggle ETBEI to force a
        // fresh edge on the PLIC source.
        if (_irq_num != 0) {
            irq_enable(_irq_num);
        }
        uart_irq_tx_disable(_device);
        uart_irq_tx_enable(_device);
    }
#endif
}
#endif

bool UARTDriver::_discard_input()
{
    if (!_initialized) {
        return false;
    }
    ring_buf_reset(&_rx_ring);
    return true;
}
