/*
 * Halve MPFS AHB from 150 MHz to 75 MHz at PRE_KERNEL_1 so the MSS I2C
 * divisor table reaches 293 kHz (PCLK/256) instead of 586 kHz (NACKs from
 * MPU-9250 / BMP280) or 156 kHz (too slow for a 1 kHz IMU).
 * Overlay pins MMUART clock-frequency = 75 MHz to match.
 */
#include <zephyr/init.h>
#include <zephyr/kernel.h>

#define MSS_SYSREG_BASE        0x20002000u
#define REG_CLOCK_CONFIG_CR    0x08u

#define DIVIDER_APB_AHB_POS    4u
#define DIVIDER_APB_AHB_MASK   (0x3u << DIVIDER_APB_AHB_POS)
#define DIVIDER_APB_AHB_DIV_8  (0x3u << DIVIDER_APB_AHB_POS)

static int bvf_set_ahb_div_8(void)
{
	uint32_t v = sys_read32(MSS_SYSREG_BASE + REG_CLOCK_CONFIG_CR);

	v &= ~DIVIDER_APB_AHB_MASK;
	v |= DIVIDER_APB_AHB_DIV_8;
	sys_write32(v, MSS_SYSREG_BASE + REG_CLOCK_CONFIG_CR);

	return 0;
}

SYS_INIT(bvf_set_ahb_div_8, PRE_KERNEL_1, 0);
