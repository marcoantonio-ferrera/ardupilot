/*
 * RETIRED — not built (see CMakeLists.txt). Halved the MSS AHB 150->75 MHz
 * so MSS I2C could reach a working divisor for the IMU/baro; no longer
 * needed now that they're on the fabric CoreI2C, and keeping AHB at 150 MHz
 * is required for USB high-speed enumeration.
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
