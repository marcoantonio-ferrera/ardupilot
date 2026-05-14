#include "PeriodicCallback.h"

#include <AP_HAL/AP_HAL.h>
#include <AP_Math/AP_Math.h>  // NEW_NOTHROW

K_THREAD_STACK_DEFINE(_ap_device_wq_stack, 16384);
struct k_work_q ap_device_wq;

static void _periodic_cb_handler(struct k_work *w)
{
    struct k_work_delayable *dw   = k_work_delayable_from_work(w);
    AP_Zephyr_PeriodicCBWork *item =
        CONTAINER_OF(dw, AP_Zephyr_PeriodicCBWork, work);

    if (item->cancelled) {
        delete item;
        return;
    }

    int64_t t0         = k_uptime_ticks();
    item->cb();
    int64_t elapsed_us = k_ticks_to_us_floor64(k_uptime_ticks() - t0);
    int64_t delay_us   = (int64_t)item->period_usec - elapsed_us;

    k_work_reschedule_for_queue(&ap_device_wq, &item->work,
                                K_USEC((uint32_t)(delay_us > 0 ? delay_us : 0)));
}

AP_HAL::Device::PeriodicHandle AP_Zephyr_register_periodic_callback(
    uint32_t period_usec, AP_HAL::Device::PeriodicCb cb)
{
    auto *item = NEW_NOTHROW AP_Zephyr_PeriodicCBWork{};
    if (!item) {
        return nullptr;
    }
    item->cb          = cb;
    item->period_usec = period_usec;
    item->cancelled   = false;
    k_work_init_delayable(&item->work, _periodic_cb_handler);
    k_work_schedule_for_queue(&ap_device_wq, &item->work,
                              K_USEC(period_usec));
    return static_cast<AP_HAL::Device::PeriodicHandle>(item);
}

bool AP_Zephyr_adjust_periodic_callback(
    AP_HAL::Device::PeriodicHandle h, uint32_t period_usec)
{
    if (!h) {
        return false;
    }
    auto *item        = static_cast<AP_Zephyr_PeriodicCBWork *>(h);
    item->period_usec = period_usec;
    k_work_reschedule_for_queue(&ap_device_wq, &item->work,
                                K_USEC(period_usec));
    return true;
}

bool AP_Zephyr_unregister_callback(AP_HAL::Device::PeriodicHandle h)
{
    if (!h) {
        return false;
    }
    auto *item      = static_cast<AP_Zephyr_PeriodicCBWork *>(h);
    item->cancelled = true;
    // if cancel caught it before it ran, free now; otherwise the handler frees it.
    if (k_work_cancel_delayable(&item->work) == 0) {
        delete item;
    }
    return true;
}

void AP_Zephyr_start_device_work_queue()
{
    static const struct k_work_queue_config cfg = {
        .name     = "ap_dev_wq",
        .no_yield = false,
    };
    k_work_queue_start(&ap_device_wq,
                       _ap_device_wq_stack,
                       K_THREAD_STACK_SIZEOF(_ap_device_wq_stack),
                       6,   /* PRIORITY_SPI */
                       &cfg);
}
