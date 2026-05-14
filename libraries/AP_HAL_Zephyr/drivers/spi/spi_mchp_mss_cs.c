/*
 * Copyright (c) 2022 Microchip Technology Inc.
 * Copyright (c) 2026 ArduPilot project
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Fork of zephyr drivers/spi/spi_mchp_mss.c for BeagleV-Fire SD-over-SPI1.
 * Compatible is ardupilot,mpfs-mss-spi. Fixes vs upstream:
 *   - GPIO CS (upstream has none); direct writes to MSS GPIO +0x88 because
 *     gpio_mchp_mss writes output values to the read-only input reg.
 *   - honour SPI_HOLD_ON_CS; force-deassert on release().
 *   - hardware SSEL kept at 0 so SPI1_SS0 (ADC) doesn't get asserted on
 *     every SD transaction.
 *   - CLK_GEN search widened from idx<16 to idx<256 so 400 kHz SD init
 *     is actually reachable on a 150 MHz APB clock.
 *   - readwr_fifo() sends 0xFF idle on pure-read and uses max(tx,rx) so
 *     spi_transceive(tx=NULL) still clocks the bus.
 *   - RX writes one byte per sample — upstream's UNALIGNED_PUT wrote 4
 *     and trampled 1-byte SD poll buffers.
 *   - disable_ints() fixed: upstream clears everything except the IRQ bits.
 *   - release() does NOT disable the controller — sdhc_spi calls it
 *     between the 74-clock init and CMD0.
 */

#define DT_DRV_COMPAT ardupilot_mpfs_mss_spi

#include <zephyr/device.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/drivers/spi/rtio.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/sys_io.h>
#include <zephyr/sys/util.h>
#include <zephyr/logging/log.h>
#include <zephyr/irq.h>

LOG_MODULE_REGISTER(mss_spi_cs, CONFIG_SPI_LOG_LEVEL);

/* Is MSS SPI module 'resets' line property defined */
#define MSS_SPI_RESET_ENABLED DT_ANY_INST_HAS_PROP_STATUS_OKAY(resets)

#if MSS_SPI_RESET_ENABLED
#include <zephyr/drivers/reset.h>
#endif

#include "spi_context.h"

/* MSS SPI Register offsets */
#define MSS_SPI_REG_CONTROL     (0x00)
#define MSS_SPI_REG_TXRXDF_SIZE (0x04)
#define MSS_SPI_REG_STATUS      (0x08)
#define MSS_SPI_REG_INT_CLEAR   (0x0c)
#define MSS_SPI_REG_RX_DATA     (0x10)
#define MSS_SPI_REG_TX_DATA     (0x14)
#define MSS_SPI_REG_CLK_GEN     (0x18)
#define MSS_SPI_REG_SS          (0x1c)
#define MSS_SPI_REG_MIS         (0x20)
#define MSS_SPI_REG_RIS         (0x24)
#define MSS_SPI_REG_CONTROL2    (0x28)
#define MSS_SPI_REG_COMMAND     (0x2c)
#define MSS_SPI_REG_PKTSIZE     (0x30)
#define MSS_SPI_REG_CMD_SIZE    (0x34)
#define MSS_SPI_REG_HWSTATUS    (0x38)
#define MSS_SPI_REG_FRAMESUP    (0x50)

/* SPICR bit definitions */
#define MSS_SPI_CONTROL_ENABLE       BIT(0)
#define MSS_SPI_CONTROL_MASTER       BIT(1)
#define MSS_SPI_CONTROL_PROTO_MSK    BIT(2)
#define MSS_SPI_CONTROL_PROTO_MOTO   (0 << 2)
#define MSS_SPI_CONTROL_RX_DATA_INT  BIT(4)
#define MSS_SPI_CONTROL_TX_DATA_INT  BIT(5)
#define MSS_SPI_CONTROL_RX_OVER_INT  BIT(6)
#define MSS_SPI_CONTROL_TX_UNDER_INT BIT(7)
#define MSS_SPI_CONTROL_CNT_MSK      (0xffff << 8)
#define MSS_SPI_CONTROL_CNT_SHF      (8)
#define MSS_SPI_CONTROL_SPO          BIT(24)
#define MSS_SPI_CONTROL_SPH          BIT(25)
#define MSS_SPI_CONTROL_SPS          BIT(26)
#define MSS_SPI_CONTROL_FRAMEURUN    BIT(27)
#define MSS_SPI_CONTROL_CLKMODE      BIT(28)
#define MSS_SPI_CONTROL_BIGFIFO      BIT(29)
#define MSS_SPI_CONTROL_OENOFF       BIT(30)
#define MSS_SPI_CONTROL_RESET        BIT(31)

/* SPIFRAMESIZE bit definitions */
#define MSS_SPI_FRAMESIZE_DEFAULT (8)

/* SPISS bit definitions */
#define MSS_SPI_SSEL_MASK (0xff)
#define MSS_SPI_DIRECT    (0x100)
#define MSS_SPI_SSELOUT   (0x200)

/* SPIST bit definitions */
#define MSS_SPI_STATUS_ACTIVE                 BIT(14)
#define MSS_SPI_STATUS_SSEL                   BIT(13)
#define MSS_SPI_STATUS_FRAMESTART             BIT(12)
#define MSS_SPI_STATUS_TXFIFO_EMPTY_NEXT_READ BIT(11)
#define MSS_SPI_STATUS_TXFIFO_EMPTY           BIT(10)
#define MSS_SPI_STATUS_TXFIFO_FULL_NEXT_WRITE BIT(9)
#define MSS_SPI_STATUS_TXFIFO_FULL            BIT(8)
#define MSS_SPI_STATUS_RXFIFO_EMPTY_NEXT_READ BIT(7)
#define MSS_SPI_STATUS_RXFIFO_EMPTY           BIT(6)
#define MSS_SPI_STATUS_RXFIFO_FULL_NEXT_WRITE BIT(5)
#define MSS_SPI_STATUS_RXFIFO_FULL            BIT(4)
#define MSS_SPI_STATUS_TX_UNDERRUN            BIT(3)
#define MSS_SPI_STATUS_RX_OVERFLOW            BIT(2)
#define MSS_SPI_STATUS_RXDAT_RCED             BIT(1)
#define MSS_SPI_STATUS_TXDAT_SENT             BIT(0)

/* SPIINT register defines */
#define MSS_SPI_INT_TXDONE       BIT(0)
#define MSS_SPI_INT_RXRDY        BIT(1)
#define MSS_SPI_INT_RX_CH_OVRFLW BIT(2)
#define MSS_SPI_INT_TX_CH_UNDRUN BIT(3)
#define MSS_SPI_INT_CMD          BIT(4)
#define MSS_SPI_INT_SSEND        BIT(5)

/* SPICOMMAND bit definitions */
#define MSS_SPI_COMMAND_FIFO_MASK (0xC)

/* SPIFRAMESUP bit definitions */
#define MSS_SPI_FRAMESUP_UP_BYTES_MSK (0xFFFF << 16)
#define MSS_SPI_FRAMESUP_LO_BYTES_MSK (0xFFFF << 0)

struct mss_spi_config {
	mm_reg_t base;
	uint8_t clk_gen;
	int clock_freq;
#if MSS_SPI_RESET_ENABLED
	struct reset_dt_spec reset_spec;
#endif
};

struct mss_spi_transfer {
	uint32_t rx_len;
	uint32_t control;
};

struct mss_spi_data {
	struct spi_context ctx;
	struct mss_spi_transfer xfer;
};

/* peek at gpio_mchp_mss's private config to grab gpio_base_addr without
 * its internal header — gpio_base_addr sits at +8 on RV64. */
struct mss_gpio_cfg_peek {
	struct gpio_driver_config common;
	uintptr_t gpio_base_addr;
};

static inline uint32_t mss_spi_read(const struct mss_spi_config *cfg, mm_reg_t offset)
{
	return sys_read32(cfg->base + offset);
}

static inline void mss_spi_write(const struct mss_spi_config *cfg, mm_reg_t offset, uint32_t val)
{
	sys_write32(val, cfg->base + offset);
}

static inline void mss_spi_hw_tfsz_set(const struct mss_spi_config *cfg, int len)
{
	uint32_t control;

	mss_spi_write(cfg, MSS_SPI_REG_FRAMESUP, (len & MSS_SPI_FRAMESUP_UP_BYTES_MSK));
	control = mss_spi_read(cfg, MSS_SPI_REG_CONTROL);
	control &= ~MSS_SPI_CONTROL_CNT_MSK;
	control |= ((len & MSS_SPI_FRAMESUP_LO_BYTES_MSK) << MSS_SPI_CONTROL_CNT_SHF);
	mss_spi_write(cfg, MSS_SPI_REG_CONTROL, control);
}

static inline void mss_spi_enable_controller(const struct mss_spi_config *cfg)
{
	uint32_t control;

	control = mss_spi_read(cfg, MSS_SPI_REG_CONTROL);
	control |= MSS_SPI_CONTROL_ENABLE;
	mss_spi_write(cfg, MSS_SPI_REG_CONTROL, control);
}

static inline void mss_spi_disable_controller(const struct mss_spi_config *cfg)
{
	uint32_t control;

	control = mss_spi_read(cfg, MSS_SPI_REG_CONTROL);
	control &= ~MSS_SPI_CONTROL_ENABLE;
	mss_spi_write(cfg, MSS_SPI_REG_CONTROL, control);
}

static void mss_spi_enable_ints(const struct mss_spi_config *cfg)
{
	uint32_t control;
	uint32_t mask = MSS_SPI_CONTROL_RX_DATA_INT | MSS_SPI_CONTROL_TX_DATA_INT |
			MSS_SPI_CONTROL_RX_OVER_INT | MSS_SPI_CONTROL_TX_UNDER_INT;

	control = mss_spi_read(cfg, MSS_SPI_REG_CONTROL);
	control |= mask;
	mss_spi_write(cfg, MSS_SPI_REG_CONTROL, control);
}

static void mss_spi_disable_ints(const struct mss_spi_config *cfg)
{
	/* upstream's `control &= ~(~mask)` reduces to `control &= int_bits`,
	 * wiping ENABLE/MASTER/SPS/BIGFIFO/CLKMODE/SPO/SPH/CNT on every end
	 * of transceive — we just want to clear the IRQ bits. */
	uint32_t mask = MSS_SPI_CONTROL_RX_DATA_INT | MSS_SPI_CONTROL_TX_DATA_INT |
			MSS_SPI_CONTROL_RX_OVER_INT | MSS_SPI_CONTROL_TX_UNDER_INT;
	uint32_t control = mss_spi_read(cfg, MSS_SPI_REG_CONTROL);

	control &= ~mask;
	mss_spi_write(cfg, MSS_SPI_REG_CONTROL, control);
}

static inline void mss_spi_readwr_fifo(const struct device *dev)
{
	const struct mss_spi_config *cfg = dev->config;
	struct mss_spi_data *data = dev->data;
	struct spi_context *ctx = &data->ctx;
	uint32_t rx_raw = 0;
	uint32_t transfer_idx = 0;
	uint32_t tx_sent = 0;
	int count;

	/* max(tx,rx) — sdhc_spi calls spi_transceive(..., NULL, &rx) for data
	 * blocks; tx-only count would exit immediately without clocking. */
	count = MAX(spi_context_total_tx_len(ctx), spi_context_total_rx_len(ctx));

	/* drain stale bytes the previous transceive left in the RX FIFO
	 * (SPI_HOLD_ON_CS keeps CS asserted so state carries over). */
	while (!(mss_spi_read(cfg, MSS_SPI_REG_STATUS) & MSS_SPI_STATUS_RXFIFO_EMPTY)) {
		(void)mss_spi_read(cfg, MSS_SPI_REG_RX_DATA);
	}

	mss_spi_hw_tfsz_set(cfg, count);

	mss_spi_enable_ints(cfg);
	while (transfer_idx < count) {
		if (!(mss_spi_read(cfg, MSS_SPI_REG_STATUS) & MSS_SPI_STATUS_RXFIFO_EMPTY)) {
			rx_raw = mss_spi_read(cfg, MSS_SPI_REG_RX_DATA);
			if (spi_context_rx_buf_on(ctx)) {
				/* one byte — upstream's UNALIGNED_PUT stored 4 and
				 * trampled sdhc_spi's 1-byte token-poll buffers. */
				*((uint8_t *)ctx->rx_buf) = (uint8_t)rx_raw;
			}
			spi_context_update_rx(ctx, 1, 1);
			++transfer_idx;
		}

		if (tx_sent < (uint32_t)count &&
		    !(mss_spi_read(cfg, MSS_SPI_REG_STATUS) & MSS_SPI_STATUS_TXFIFO_FULL)) {
			if (spi_context_tx_on(ctx)) {
				if (spi_context_tx_buf_on(ctx)) {
					mss_spi_write(cfg, MSS_SPI_REG_TX_DATA,
						      ctx->tx_buf[0]);
				} else {
					/* NULL .buf with nonzero .len: SD-SPI needs MOSI high */
					mss_spi_write(cfg, MSS_SPI_REG_TX_DATA, 0xFFu);
				}
				spi_context_update_tx(ctx, 1, 1);
			} else {
				/* TX exhausted (or absent) — still clock the bus
				 * to drain RX; don't update_tx. */
				mss_spi_write(cfg, MSS_SPI_REG_TX_DATA, 0xFFu);
			}
			++tx_sent;
		}
	}
}

/* force hw SSEL off — SPI1_SS0 is wired to the ADC on this board, and
 * we drive all CS lines from GPIO instead. */
static inline int mss_spi_disable_hw_ssel(const struct mss_spi_config *cfg)
{
	uint32_t reg = mss_spi_read(cfg, MSS_SPI_REG_SS);

	reg &= ~MSS_SPI_SSEL_MASK;
	mss_spi_write(cfg, MSS_SPI_REG_SS, reg);
	return 0;
}

/* CS toggle via MSS GPIO output reg (+0x88). Replaces spi_context_cs_control()
 * which no-ops here — gpio_mchp_mss writes outputs to +0x84 (input reg).
 * Honours SPI_HOLD_ON_CS on deassert; release() uses cs_force_deassert(). */
static inline void mss_spi_cs_control_fix(struct spi_context *ctx, bool on)
{
	const struct gpio_dt_spec *cs = &ctx->config->cs.gpio;

	if (cs->port == NULL) {
		return;
	}

	if (!on && ctx->config && (ctx->config->operation & SPI_HOLD_ON_CS)) {
		return;
	}

	const struct mss_gpio_cfg_peek *gcfg =
		(const struct mss_gpio_cfg_peek *)cs->port->config;
	uintptr_t base = gcfg->gpio_base_addr;
	uint32_t outp = sys_read32(base + 0x88u);
	bool active_low = (cs->dt_flags & GPIO_ACTIVE_LOW) != 0;
	bool pin_high = (bool)on ^ active_low;

	if (pin_high) {
		outp |= BIT(cs->pin);
	} else {
		outp &= ~BIT(cs->pin);
	}
	sys_write32(outp, base + 0x88u);
}

/* unconditional CS deassert for spi_release() — bypasses SPI_HOLD_ON_CS. */
static inline void mss_spi_cs_force_deassert(struct spi_context *ctx)
{
	if (!ctx->config) {
		return;
	}
	const struct gpio_dt_spec *cs = &ctx->config->cs.gpio;
	if (cs->port == NULL) {
		return;
	}
	const struct mss_gpio_cfg_peek *gcfg =
		(const struct mss_gpio_cfg_peek *)cs->port->config;
	uintptr_t base = gcfg->gpio_base_addr;
	uint32_t outp = sys_read32(base + 0x88u);
	bool active_low = (cs->dt_flags & GPIO_ACTIVE_LOW) != 0;

	if (active_low) {
		outp |= BIT(cs->pin);
	} else {
		outp &= ~BIT(cs->pin);
	}
	sys_write32(outp, base + 0x88u);
}

static inline int mss_spi_clk_gen_set(const struct mss_spi_config *cfg,
				      const struct spi_config *spi_cfg)
{
	uint32_t idx, clkrate, val = 0;

	/* upstream limits idx<16 — can't reach 400 kHz on a 150 MHz APB clock,
	 * so SD init CMD8 runs at 75 MHz and the card can't answer.
	 * SPICLK = APBCLK / (2 * idx) with CLKMODE=1. */
	for (idx = 1; idx < 256; idx++) {
		clkrate = cfg->clock_freq / (2 * idx);
		if (clkrate <= spi_cfg->frequency) {
			val = idx;
			break;
		}
	}

	mss_spi_write(cfg, MSS_SPI_REG_CLK_GEN, val);

	return 0;
}

static inline int mss_spi_hw_mode_set(const struct mss_spi_config *cfg, unsigned int mode)
{
	uint32_t control = mss_spi_read(cfg, MSS_SPI_REG_CONTROL);

	if (mode & SPI_MODE_CPHA) {
		control |= MSS_SPI_CONTROL_SPH;
	} else {
		control &= ~MSS_SPI_CONTROL_SPH;
	}

	if (mode & SPI_MODE_CPOL) {
		control |= MSS_SPI_CONTROL_SPO;
	} else {
		control &= ~MSS_SPI_CONTROL_SPO;
	}

	mss_spi_write(cfg, MSS_SPI_REG_CONTROL, control);

	return 0;
}

static void mss_spi_interrupt(const struct device *dev)
{
	const struct mss_spi_config *cfg = dev->config;
	struct mss_spi_data *data = dev->data;
	struct spi_context *ctx = &data->ctx;
	int intfield = mss_spi_read(cfg, MSS_SPI_REG_MIS) & 0xf;

	if (intfield == 0) {
		return;
	}

	mss_spi_write(cfg, MSS_SPI_REG_INT_CLEAR, intfield);
	spi_context_complete(ctx, dev, 0);
}

static int mss_spi_release(const struct device *dev, const struct spi_config *config)
{
	const struct mss_spi_config *cfg = dev->config;
	struct mss_spi_data *data = dev->data;

	mss_spi_disable_ints(cfg);

	/* release = drop CS, even if the last transceive held it via SPI_HOLD_ON_CS */
	mss_spi_cs_force_deassert(&data->ctx);

	/* do NOT disable the controller here — sdhc_spi calls release() between
	 * the 74-clock init and CMD0, and CONTROL.ENABLE=0 hangs the CMD0 clock. */
	spi_context_unlock_unconditionally(&data->ctx);

	return 0;
}

static int mss_spi_configure(const struct device *dev, const struct spi_config *spi_cfg)
{
	const struct mss_spi_config *cfg = dev->config;
	struct mss_spi_data *data = dev->data;
	struct spi_context *ctx = &data->ctx;
	struct mss_spi_transfer *xfer = &data->xfer;

	if (spi_cfg->operation & (SPI_TRANSFER_LSB | SPI_OP_MODE_SLAVE | SPI_MODE_LOOP)) {
		LOG_WRN("not supported operation\n\r");
		return -ENOTSUP;
	}

	if (SPI_WORD_SIZE_GET(spi_cfg->operation) != MSS_SPI_FRAMESIZE_DEFAULT) {
		return -ENOTSUP;
	}

	ctx->config = spi_cfg;

	mss_spi_disable_hw_ssel(cfg);

	/* preserve ENABLE across configure — upstream's disable/enable cycle
	 * glitches SCK mid-transaction under SPI_HOLD_ON_CS and shifts bits. */
	uint32_t cur_ctrl = mss_spi_read(cfg, MSS_SPI_REG_CONTROL);
	mss_spi_write(cfg, MSS_SPI_REG_CONTROL,
		      xfer->control | (cur_ctrl & MSS_SPI_CONTROL_ENABLE));

	if (mss_spi_clk_gen_set(cfg, spi_cfg)) {
		LOG_ERR("can't set clk divider\n");
		return -EINVAL;
	}

	mss_spi_hw_mode_set(cfg, spi_cfg->operation);
	mss_spi_write(cfg, MSS_SPI_REG_TXRXDF_SIZE, MSS_SPI_FRAMESIZE_DEFAULT);
	mss_spi_enable_controller(cfg);
	mss_spi_write(cfg, MSS_SPI_REG_COMMAND, MSS_SPI_COMMAND_FIFO_MASK);

	return 0;
}

static int mss_spi_transceive(const struct device *dev, const struct spi_config *spi_cfg,
			      const struct spi_buf_set *tx_bufs, const struct spi_buf_set *rx_bufs,
			      bool async, spi_callback_t cb, void *userdata)
{
	const struct mss_spi_config *config = dev->config;
	struct mss_spi_data *data = dev->data;
	struct spi_context *ctx = &data->ctx;
	struct mss_spi_transfer *xfer = &data->xfer;
	int ret = 0;

	spi_context_lock(ctx, async, cb, userdata, spi_cfg);

	ret = mss_spi_configure(dev, spi_cfg);
	if (ret) {
		LOG_ERR("Fail to configure\n\r");
		goto out;
	}

	spi_context_buffers_setup(ctx, tx_bufs, rx_bufs, 1);
	xfer->rx_len = ctx->rx_len;

	if (!(spi_cfg->operation & SPI_CS_ACTIVE_HIGH)) {
		mss_spi_cs_control_fix(ctx, true);
	}

	mss_spi_readwr_fifo(dev);
	ret = spi_context_wait_for_completion(ctx);

	if (!(spi_cfg->operation & SPI_CS_ACTIVE_HIGH)) {
		mss_spi_cs_control_fix(ctx, false);
	}
out:
	spi_context_release(ctx, ret);
	mss_spi_disable_ints(config);

	return ret;
}

static int mss_spi_transceive_blocking(const struct device *dev, const struct spi_config *spi_cfg,
				       const struct spi_buf_set *tx_bufs,
				       const struct spi_buf_set *rx_bufs)
{
	return mss_spi_transceive(dev, spi_cfg, tx_bufs, rx_bufs, false, NULL, NULL);
}

#ifdef CONFIG_SPI_ASYNC
static int mss_spi_transceive_async(const struct device *dev, const struct spi_config *spi_cfg,
				    const struct spi_buf_set *tx_bufs,
				    const struct spi_buf_set *rx_bufs, spi_callback_t cb,
				    void *userdata)
{
	return mss_spi_transceive(dev, spi_cfg, tx_bufs, rx_bufs, true, cb, userdata);
}
#endif /* CONFIG_SPI_ASYNC */

static int mss_spi_init(const struct device *dev)
{
	const struct mss_spi_config *cfg = dev->config;
	struct mss_spi_data *data = dev->data;
	struct mss_spi_transfer *xfer = &data->xfer;
	int ret = 0;
	uint32_t control = 0;

#if MSS_SPI_RESET_ENABLED
	if (cfg->reset_spec.dev != NULL) {
		(void)reset_line_deassert_dt(&cfg->reset_spec);
	}
#endif

	/* Remove SPI from Reset */
	control = mss_spi_read(cfg, MSS_SPI_REG_CONTROL);
	control &= ~MSS_SPI_CONTROL_RESET;
	mss_spi_write(cfg, MSS_SPI_REG_CONTROL, control);

	/* Set master mode */
	mss_spi_disable_controller(cfg);
	xfer->control = (MSS_SPI_CONTROL_SPS | MSS_SPI_CONTROL_BIGFIFO | MSS_SPI_CONTROL_MASTER |
			 MSS_SPI_CONTROL_CLKMODE);

	mss_spi_disable_hw_ssel(cfg);

	spi_context_unlock_unconditionally(&data->ctx);

	/* configures the gpio_dt_spec; the pin is actually driven by
	 * mss_spi_cs_control_fix() on each transfer. */
	ret = spi_context_cs_configure_all(&data->ctx);

	LOG_INF("init ret=%d CONTROL=0x%08x",
		ret, mss_spi_read(cfg, MSS_SPI_REG_CONTROL));

	return ret;
}

#define MICROCHIP_SPI_PM_OPS (NULL)

static DEVICE_API(spi, mss_spi_driver_api) = {
	.transceive = mss_spi_transceive_blocking,
#ifdef CONFIG_SPI_ASYNC
	.transceive_async = mss_spi_transceive_async,
#endif /* CONFIG_SPI_ASYNC */
#ifdef CONFIG_SPI_RTIO
	.iodev_submit = spi_rtio_iodev_default_submit,
#endif
	.release = mss_spi_release,
};

#define MSS_SPI_INIT(n)                                                                            \
	static int mss_spi_init_##n(const struct device *dev)                                      \
	{                                                                                          \
		int _ret = mss_spi_init(dev);                                                      \
                                                                                                   \
		IRQ_CONNECT(DT_INST_IRQN(n), DT_INST_IRQ(n, priority), mss_spi_interrupt,          \
			    DEVICE_DT_INST_GET(n), 0);                                             \
                                                                                                   \
		irq_enable(DT_INST_IRQN(n));                                                       \
                                                                                                   \
		return _ret;                                                                       \
	}                                                                                          \
                                                                                                   \
	static const struct mss_spi_config mss_spi_config_##n = {                                  \
		.base = DT_INST_REG_ADDR(n),                                                       \
		.clock_freq = DT_INST_PROP(n, clock_frequency),                                    \
		IF_ENABLED(DT_INST_NODE_HAS_PROP(n, resets),                                       \
			(.reset_spec = RESET_DT_SPEC_INST_GET(n),))                                \
	};                                                                                         \
                                                                                                   \
	static struct mss_spi_data mss_spi_data_##n = {                                            \
		SPI_CONTEXT_INIT_LOCK(mss_spi_data_##n, ctx),                                      \
		SPI_CONTEXT_INIT_SYNC(mss_spi_data_##n, ctx),                                      \
		SPI_CONTEXT_CS_GPIOS_INITIALIZE(DT_DRV_INST(n), ctx)                               \
	};                                                                                         \
                                                                                                   \
	SPI_DEVICE_DT_INST_DEFINE(n, mss_spi_init_##n, NULL, &mss_spi_data_##n,                    \
				  &mss_spi_config_##n, POST_KERNEL,                                \
				  CONFIG_KERNEL_INIT_PRIORITY_DEVICE, &mss_spi_driver_api);

DT_INST_FOREACH_STATUS_OKAY(MSS_SPI_INIT)
