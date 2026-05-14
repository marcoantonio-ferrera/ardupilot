#include "SPIDevice.h"
#include "PeriodicCallback.h"

#include <zephyr/drivers/spi.h>
#include <zephyr/sys/printk.h>
#include <string.h>

using namespace Zephyr;

// board-specific entries are injected via HAL_ZEPHYR_SPI_DEVICES in hwdef.h
struct SPIDesc {
    const char *name;
    uint8_t     bus;
    uint8_t     cs;
    uint32_t    lowspeed;
    uint32_t    highspeed;
    uint8_t     mode;
};

#ifdef HAL_ZEPHYR_SPI_DEVICES
static const SPIDesc device_table[] = {
    HAL_ZEPHYR_SPI_DEVICES
};
static constexpr uint8_t NUM_DEVICES = sizeof(device_table) / sizeof(device_table[0]);
#else
// no SPI sensors on this board — pointer stand-in to avoid a zero-size array
static const SPIDesc *device_table   = nullptr;
static constexpr uint8_t NUM_DEVICES = 0;
#endif

const struct device *SPIDeviceManager::_bus_device(uint8_t bus)
{
    switch (bus) {
#if defined(CONFIG_SPI) && DT_HAS_ALIAS(ardupilot_spi0) && DT_NODE_HAS_STATUS(DT_ALIAS(ardupilot_spi0), okay)
    case 0: return DEVICE_DT_GET(DT_ALIAS(ardupilot_spi0));
#endif
#if defined(CONFIG_SPI) && DT_HAS_ALIAS(ardupilot_spi1) && DT_NODE_HAS_STATUS(DT_ALIAS(ardupilot_spi1), okay)
    case 1: return DEVICE_DT_GET(DT_ALIAS(ardupilot_spi1));
#endif
    default: return nullptr;
    }
}

SPIDevice::SPIDevice(const struct device *dev, uint8_t cs,
                     uint32_t lowspeed, uint32_t highspeed, uint8_t mode) :
    _dev(dev),
    _cs(cs),
    _lowspeed(lowspeed),
    _highspeed(highspeed),
    _mode(mode),
    _high_speed(true)
{
}

bool SPIDevice::set_speed(AP_HAL::Device::Speed speed)
{
    _high_speed = (speed == AP_HAL::Device::SPEED_HIGH);
    return true;
}

AP_HAL::Semaphore *SPIDevice::get_semaphore()
{
    return &_semaphore;
}

AP_HAL::Device::PeriodicHandle SPIDevice::register_periodic_callback(
    uint32_t period_usec, AP_HAL::Device::PeriodicCb cb)
{
    return AP_Zephyr_register_periodic_callback(period_usec, cb);
}

bool SPIDevice::adjust_periodic_callback(PeriodicHandle h, uint32_t period_usec)
{
    return AP_Zephyr_adjust_periodic_callback(h, period_usec);
}

bool SPIDevice::unregister_callback(PeriodicHandle h)
{
    return AP_Zephyr_unregister_callback(h);
}

static void build_spi_config(struct spi_config *cfg, uint8_t cs,
                              uint32_t freq, uint8_t mode)
{
    uint16_t op = SPI_OP_MODE_MASTER | SPI_WORD_SET(8) | SPI_LINES_SINGLE;
    if (mode & 0x1u) { op |= SPI_MODE_CPHA; }
    if (mode & 0x2u) { op |= SPI_MODE_CPOL; }
    cfg->frequency = freq;
    cfg->operation = op;
    cfg->slave     = cs;
    memset(&cfg->cs, 0, sizeof(cfg->cs));
}

bool SPIDevice::transfer(const uint8_t *send, uint32_t send_len,
                         uint8_t *recv, uint32_t recv_len)
{
#ifdef CONFIG_SPI
    if (_dev == nullptr || !device_is_ready(_dev)) {
        return false;
    }

    struct spi_config spi_cfg;
    build_spi_config(&spi_cfg, _cs,
                     _high_speed ? _highspeed : _lowspeed, _mode);

    struct spi_buf tx_bufs[2];
    struct spi_buf rx_bufs[2];
    uint8_t num_tx = 0;
    uint8_t num_rx = 0;

    if (send != nullptr && send_len > 0) {
        tx_bufs[num_tx].buf = const_cast<uint8_t *>(send);
        tx_bufs[num_tx].len = send_len;
        num_tx++;
        rx_bufs[num_rx].buf = nullptr;
        rx_bufs[num_rx].len = send_len;
        num_rx++;
    }

    if (recv != nullptr && recv_len > 0) {
        tx_bufs[num_tx].buf = nullptr;
        tx_bufs[num_tx].len = recv_len;
        num_tx++;
        rx_bufs[num_rx].buf = recv;
        rx_bufs[num_rx].len = recv_len;
        num_rx++;
    }

    if (num_tx == 0 && num_rx == 0) {
        return true;
    }

    const struct spi_buf_set tx_set = { tx_bufs, num_tx };
    const struct spi_buf_set rx_set = { rx_bufs, num_rx };

    return spi_transceive(_dev, &spi_cfg, &tx_set, &rx_set) == 0;
#else
    (void)send; (void)send_len; (void)recv; (void)recv_len;
    return false;
#endif
}

bool SPIDevice::transfer_fullduplex(const uint8_t *send, uint8_t *recv,
                                    uint32_t len)
{
#ifdef CONFIG_SPI
    if (_dev == nullptr || !device_is_ready(_dev)) {
        return false;
    }

    struct spi_config spi_cfg;
    build_spi_config(&spi_cfg, _cs,
                     _high_speed ? _highspeed : _lowspeed, _mode);

    struct spi_buf tx_buf = { const_cast<uint8_t *>(send), len };
    struct spi_buf rx_buf = { recv, len };

    const struct spi_buf_set tx_set = { &tx_buf, 1 };
    const struct spi_buf_set rx_set = { &rx_buf, 1 };

    return spi_transceive(_dev, &spi_cfg, &tx_set, &rx_set) == 0;
#else
    (void)send; (void)recv; (void)len;
    return false;
#endif
}

AP_HAL::SPIDevice *SPIDeviceManager::get_device_ptr(const char *name)
{
    for (uint8_t i = 0; i < NUM_DEVICES; i++) {
        if (strcmp(device_table[i].name, name) == 0) {
            const struct device *dev = _bus_device(device_table[i].bus);
            if (dev == nullptr) {
                printk("AP_HAL_Zephyr SPI: bus %u not available for '%s'\n",
                       device_table[i].bus, name);
            }
            return NEW_NOTHROW SPIDevice(dev,
                                        device_table[i].cs,
                                        device_table[i].lowspeed,
                                        device_table[i].highspeed,
                                        device_table[i].mode);
        }
    }

    printk("AP_HAL_Zephyr SPI: unknown device '%s'\n", name);
    return nullptr;
}
