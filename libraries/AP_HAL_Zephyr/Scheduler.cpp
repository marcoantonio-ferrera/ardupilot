#include "Scheduler.h"
#include "PeriodicCallback.h"

#include <zephyr/sys/printk.h>
#include <zephyr/devicetree.h>

using namespace Zephyr;

// K_THREAD_STACK_DEFINE needs integer literals; keep in sync with Scheduler.h.
K_THREAD_STACK_DEFINE(scheduler_timer_stack, 4096);
K_THREAD_STACK_DEFINE(scheduler_io_stack, 8192);
K_THREAD_STACK_ARRAY_DEFINE(scheduler_user_stacks, 6, 8192);

// k_timer fires at the exact period; expiry only does k_sem_give (ISR-safe).
static K_SEM_DEFINE(s_timer_sem, 0, 2);
static K_SEM_DEFINE(s_io_sem,    0, 2);

static void timer_expiry_fn(struct k_timer *) { k_sem_give(&s_timer_sem); }
static void io_expiry_fn(struct k_timer *)    { k_sem_give(&s_io_sem);    }

static K_TIMER_DEFINE(s_hal_timer, timer_expiry_fn, NULL);
static K_TIMER_DEFINE(s_hal_io,    io_expiry_fn,    NULL);

static void timer_thread_fn(void *arg, void *, void *)
{
    auto *sched = static_cast<Zephyr::Scheduler *>(arg);
    while (true) {
        k_sem_take(&s_timer_sem, K_FOREVER);
        sched->_run_timer_procs();
    }
}

static void io_thread_fn(void *arg, void *, void *)
{
    auto *sched = static_cast<Zephyr::Scheduler *>(arg);
    while (true) {
        k_sem_take(&s_io_sem, K_FOREVER);
        sched->_run_io_procs();
    }
}

static void user_thread_fn(void *arg, void *, void *)
{
    AP_HAL::MemberProc *proc = static_cast<AP_HAL::MemberProc *>(arg);
    (*proc)();
}

Scheduler::Scheduler() :
    _callbacks(nullptr),
    _num_timer_procs(0),
    _num_io_procs(0),
    _failsafe(nullptr),
    _failsafe_period_us(0),
    _last_failsafe_us(0),
    _system_initialized(false),
    _param_count_dirty(false),
    _init_ms(0),
    _main_thread(nullptr),
    _argc(0),
    _argv(nullptr)
{
    for (auto &t : _user_threads) {
        t.in_use = false;
    }
}

void Scheduler::init()
{
    _main_thread = k_current_get();

    k_thread_create(&_timer_thread_data,
                    scheduler_timer_stack,
                    K_THREAD_STACK_SIZEOF(scheduler_timer_stack),
                    timer_thread_fn, this, nullptr, nullptr,
                    TIMER_THREAD_PRIO, 0, K_NO_WAIT);
    k_thread_name_set(&_timer_thread_data, "ap_timer");

    k_thread_create(&_io_thread_data,
                    scheduler_io_stack,
                    K_THREAD_STACK_SIZEOF(scheduler_io_stack),
                    io_thread_fn, this, nullptr, nullptr,
                    IO_THREAD_PRIO, 0, K_NO_WAIT);
    k_thread_name_set(&_io_thread_data, "ap_io");

    AP_Zephyr_start_device_work_queue();

    k_timer_start(&s_hal_timer, K_USEC(1000), K_USEC(1000));
    k_timer_start(&s_hal_io,    K_USEC(1000), K_USEC(1000));
}

void Scheduler::delay(uint16_t ms)
{
    if (ms >= _min_delay_cb_ms) {
        call_delay_cb();
    }
    k_sleep(K_MSEC(ms));
}

void Scheduler::delay_microseconds(uint16_t us)
{
    k_busy_wait(us);
}

void Scheduler::register_timer_process(AP_HAL::MemberProc proc)
{
    if (_num_timer_procs < MAX_TIMER_PROCS) {
        _timer_procs[_num_timer_procs++] = proc;
    }
}

void Scheduler::register_io_process(AP_HAL::MemberProc proc)
{
    if (_num_io_procs < MAX_IO_PROCS) {
        _io_procs[_num_io_procs++] = proc;
    }
}

void Scheduler::register_timer_failsafe(AP_HAL::Proc proc, uint32_t period_us)
{
    _failsafe = proc;
    _failsafe_period_us = period_us;
}

void Scheduler::set_system_initialized()
{
    // EKF3 may drop ~82 params on first loop() when it disables itself
    // after a failed heap alloc; tick() keeps invalidating the param
    // count cache for 3 s so the post-EKF snapshot is the one the GCS sees.
    _init_ms = k_uptime_get_32();
    _param_count_dirty = true;

    _system_initialized = true;

#ifdef AP_MAV_DEFAULT_STREAM_RATE_PARAMS
    // raise any stored MAVn_PARAMS below the compile-time default;
    // too low pins param streaming at MP's timeout and the download
    // never completes. User-raised values are left alone.
    static const char * const sr_params[] = {
        "MAV1_PARAMS", "MAV2_PARAMS", "MAV3_PARAMS",
        "MAV4_PARAMS", "MAV5_PARAMS",
    };
    for (const char *name : sr_params) {
        enum ap_var_type ptype;
        AP_Param *p = AP_Param::find(name, &ptype);
        int cur = (p != nullptr) ? (int)p->cast_to_float(ptype) : -1;
        printk("AP_HAL_Zephyr: %s=%d", name, cur);
        if (p != nullptr && cur < AP_MAV_DEFAULT_STREAM_RATE_PARAMS) {
            p->set_float((float)AP_MAV_DEFAULT_STREAM_RATE_PARAMS, ptype);
            p->save();
            printk(" -> %d (raised)\n", AP_MAV_DEFAULT_STREAM_RATE_PARAMS);
        } else {
            printk(" (ok)\n");
        }
    }

    // pre-EKF snapshot — tick() will keep invalidating for 3 s
    uint16_t param_count = AP_Param::count_parameters();
    printk("AP_HAL_Zephyr: param_count=%u (pre-EKF-init snapshot)\n", (unsigned)param_count);
#endif
}

bool Scheduler::is_system_initialized()
{
    return _system_initialized;
}

void Scheduler::reboot(bool hold_in_bootloader)
{
    (void)(hold_in_bootloader);

    // no clean reboot path on MPFS AMP with skip-opensbi=true (no SBI,
    // MSS_RESET_CR takes Linux with us, __reset leaves peripherals mid-transaction).
    // Spin and let the user power-cycle.
    printk("AP_HAL_Zephyr: reboot not supported on this config; power-cycle required\n");
    irq_lock();

    for (;;) {
        __asm__ __volatile__("" ::: "memory");
    }
}

bool Scheduler::in_main_thread() const
{
    return k_current_get() == _main_thread;
}

bool Scheduler::thread_create(AP_HAL::MemberProc proc, const char *name,
                               uint32_t stack_size,
                               priority_base base, int8_t priority)
{
    for (uint8_t i = 0; i < MAX_USER_THREADS; i++) {
        if (_user_threads[i].in_use) {
            continue;
        }

        if (stack_size > USER_STACK_SIZE) {
            printk("AP_HAL_Zephyr: thread '%s' requested %u B stack, capped at %u B\n",
                   name, (unsigned)stack_size, (unsigned)USER_STACK_SIZE);
        }

        _user_threads[i].proc   = proc;
        _user_threads[i].in_use = true;

        k_thread_create(&_user_threads[i].thread_data,
                        scheduler_user_stacks[i],
                        USER_STACK_SIZE,
                        user_thread_fn, &_user_threads[i].proc,
                        nullptr, nullptr,
                        _zephyr_priority(base, priority), 0, K_NO_WAIT);
        k_thread_name_set(&_user_threads[i].thread_data, name);
        printk("AP_HAL_Zephyr: thread '%s' created (slot=%u prio=%d)\n",
               name, (unsigned)i, _zephyr_priority(base, priority));
        return true;
    }

    printk("AP_HAL_Zephyr: thread pool full, cannot create '%s'\n", name);
    return false;
}

void *Scheduler::disable_interrupts_save()
{
    unsigned int key = irq_lock();
    return reinterpret_cast<void *>(static_cast<uintptr_t>(key));
}

void Scheduler::restore_interrupts(void *state)
{
    irq_unlock(static_cast<unsigned int>(reinterpret_cast<uintptr_t>(state)));
}

void Scheduler::set_callbacks(AP_HAL::HAL::Callbacks *cb)
{
    _callbacks = cb;
}

void Scheduler::set_arguments(int argc, char *const *argv)
{
    _argc = argc;
    _argv = argv;
}

void Scheduler::tick()
{
    if (_param_count_dirty) {
        if (k_uptime_get_32() - _init_ms < 3000U) {
            AP_Param::invalidate_count();
        } else {
            _param_count_dirty = false;
        }
    }
    k_yield();
}

void Scheduler::_run_timer_procs()
{
    for (uint8_t i = 0; i < _num_timer_procs; i++) {
        _timer_procs[i]();
    }

    if (_failsafe != nullptr && _failsafe_period_us != 0) {
        const uint64_t now = k_ticks_to_us_floor64(k_uptime_ticks());
        if ((now - _last_failsafe_us) >= _failsafe_period_us) {
            _last_failsafe_us = now;
            _failsafe();
        }
    }
}

void Scheduler::_run_io_procs()
{
    for (uint8_t i = 0; i < _num_io_procs; i++) {
        _io_procs[i]();
    }
}

void Scheduler::commandline_arguments(uint8_t &argc, char *const *&argv) const
{
    argc = static_cast<uint8_t>(_argc);
    argv = _argv;
}

int Scheduler::_zephyr_priority(priority_base base, int8_t offset)
{
    int base_prio;
    switch (base) {
    case PRIORITY_BOOST:     base_prio = 1;  break;
    case PRIORITY_TIMER:     base_prio = 2;  break;
    case PRIORITY_RCOUT:     base_prio = 3;  break;
    case PRIORITY_RCIN:      base_prio = 4;  break;
    case PRIORITY_CAN:       base_prio = 5;  break;
    case PRIORITY_SPI:       base_prio = 6;  break;
    case PRIORITY_I2C:       base_prio = 7;  break;
    case PRIORITY_MAIN:      base_prio = 8;  break;
    case PRIORITY_UART:      base_prio = 9;  break;
    // PRIORITY_IO raised above MAIN: at prio 10 the compasscal/FTP/logger
    // threads never got dispatched with our 1 kHz timers + 400 Hz fast-loop.
    // This inverts the usual "IO below MAIN" but it's the only way to get
    // prio-10 user threads to run on this platform.
    case PRIORITY_IO:        base_prio = 7;  break;
    case PRIORITY_STORAGE:   base_prio = 12; break;
    case PRIORITY_SCRIPTING: base_prio = 13; break;
    case PRIORITY_NET:       base_prio = 13; break;
    case PRIORITY_LED:       base_prio = 14; break;
    default:                 base_prio = 10; break;
    }

    int prio = base_prio - static_cast<int>(offset);
    if (prio < 0)  { prio = 0;  }
    if (prio > 15) { prio = 15; }
    return prio;
}
