/*
 * Zephyr PWM driver for the BeagleV-Fire ROBOTICS fabric servo block.
 * 8 channels, fixed 50 Hz; reg = pulse_us - 900 (hw adds the 900 us base).
 * Virtual clock is 1 MHz so Zephyr's pwm_set() gives us pulse_cycles == us.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT beaglev_fire_fabric_servo_pwm

#include <zephyr/device.h>
#include <zephyr/drivers/pwm.h>
#include <zephyr/sys/sys_io.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(pwm_fabric_servo, CONFIG_PWM_LOG_LEVEL);

#define FABRIC_SERVO_CHANNELS    8u
#define FABRIC_SERVO_OFFSET_US   900u   /* hw pulse base */
#define FABRIC_SERVO_MAX_HW_VAL  1200u  /* caps pulse at 2.1 ms */
#define FABRIC_SERVO_VIRTUAL_HZ  1000000u

struct pwm_fabric_servo_cfg {
    uint32_t base;
};

static int pwm_fabric_servo_set_cycles(const struct device *dev,
                                       uint32_t channel,
                                       uint32_t period_cycles,
                                       uint32_t pulse_cycles,
                                       pwm_flags_t flags)
{
    const struct pwm_fabric_servo_cfg *cfg = dev->config;

    if (channel >= FABRIC_SERVO_CHANNELS) {
        LOG_ERR("channel %u out of range (max %u)",
                channel, FABRIC_SERVO_CHANNELS - 1u);
        return -EINVAL;
    }

    if (flags & PWM_POLARITY_INVERTED) {
        return -ENOTSUP;
    }

    /* period is fixed at 20 ms in hardware */
    (void)period_cycles;

    uint32_t hw_val = 0u;
    if (pulse_cycles > FABRIC_SERVO_OFFSET_US) {
        hw_val = pulse_cycles - FABRIC_SERVO_OFFSET_US;
        if (hw_val > FABRIC_SERVO_MAX_HW_VAL) {
            hw_val = FABRIC_SERVO_MAX_HW_VAL;
        }
    }

    sys_write32(hw_val, cfg->base + channel * 4u);

    LOG_DBG("ch=%u pulse_cycles=%u hw_val=%u", channel, pulse_cycles, hw_val);
    return 0;
}

static int pwm_fabric_servo_get_cycles_per_sec(const struct device *dev,
                                               uint32_t channel,
                                               uint64_t *cycles)
{
    if (channel >= FABRIC_SERVO_CHANNELS) {
        return -EINVAL;
    }

    *cycles = FABRIC_SERVO_VIRTUAL_HZ;
    return 0;
}

static int pwm_fabric_servo_init(const struct device *dev)
{
    const struct pwm_fabric_servo_cfg *cfg = dev->config;

    for (uint32_t ch = 0u; ch < FABRIC_SERVO_CHANNELS; ch++) {
        sys_write32(0u, cfg->base + ch * 4u);
    }

    LOG_INF("fabric servo PWM @ 0x%08x (%u channels)",
            cfg->base, FABRIC_SERVO_CHANNELS);
    return 0;
}

static DEVICE_API(pwm, pwm_fabric_servo_api) = {
    .set_cycles         = pwm_fabric_servo_set_cycles,
    .get_cycles_per_sec = pwm_fabric_servo_get_cycles_per_sec,
};

#define PWM_FABRIC_SERVO_INIT(n)                                              \
    static const struct pwm_fabric_servo_cfg pwm_fabric_servo_cfg_##n = {    \
        .base = DT_INST_REG_ADDR(n),                                          \
    };                                                                        \
    DEVICE_DT_INST_DEFINE(n,                                                  \
                          pwm_fabric_servo_init,                              \
                          NULL,                                               \
                          NULL,                                               \
                          &pwm_fabric_servo_cfg_##n,                          \
                          POST_KERNEL,                                        \
                          CONFIG_PWM_INIT_PRIORITY,                           \
                          &pwm_fabric_servo_api);

DT_INST_FOREACH_STATUS_OKAY(PWM_FABRIC_SERVO_INIT)
