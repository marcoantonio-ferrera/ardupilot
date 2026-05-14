#pragma once

#include <zephyr/kernel.h>
#include <zephyr/sys/reboot.h>

#include <AP_HAL/AP_HAL.h>
#include "AP_HAL_Zephyr_Namespace.h"

class Zephyr::Scheduler : public AP_HAL::Scheduler {
public:
    Scheduler();

    void init() override;
    void delay(uint16_t ms) override;
    void delay_microseconds(uint16_t us) override;
    void register_timer_process(AP_HAL::MemberProc proc) override;
    void register_io_process(AP_HAL::MemberProc proc) override;
    void register_timer_failsafe(AP_HAL::Proc proc, uint32_t period_us) override;
    void set_system_initialized() override;
    bool is_system_initialized() override;
    void reboot(bool hold_in_bootloader) override;
    bool in_main_thread() const override;
    bool thread_create(AP_HAL::MemberProc proc, const char *name, uint32_t stack_size,
                       priority_base base, int8_t priority) override;
    void *disable_interrupts_save() override;
    void restore_interrupts(void *state) override;

    void set_callbacks(AP_HAL::HAL::Callbacks *cb);
    void set_arguments(int argc, char *const *argv);
    void tick();
    void commandline_arguments(uint8_t &argc, char *const *&argv) const;

    void _run_timer_procs();  // called from static thread trampolines
    void _run_io_procs();

private:
    static constexpr uint8_t MAX_TIMER_PROCS = 8;
    static constexpr uint8_t MAX_IO_PROCS    = 8;
    static constexpr uint8_t MAX_USER_THREADS = 6;

    static constexpr size_t TIMER_STACK_SIZE = 4096;
    static constexpr size_t IO_STACK_SIZE    = 8192;
    static constexpr size_t USER_STACK_SIZE  = 8192;

    // 0 = highest; main runs at 8 (CONFIG_MAIN_THREAD_PRIORITY)
    static constexpr int TIMER_THREAD_PRIO = 2;
    static constexpr int IO_THREAD_PRIO    = 5;

    AP_HAL::HAL::Callbacks *_callbacks;

    AP_HAL::MemberProc _timer_procs[MAX_TIMER_PROCS];
    AP_HAL::MemberProc _io_procs[MAX_IO_PROCS];
    uint8_t            _num_timer_procs;
    uint8_t            _num_io_procs;

    AP_HAL::Proc _failsafe;
    uint32_t     _failsafe_period_us;
    uint64_t     _last_failsafe_us;

    bool     _system_initialized;
    bool     _param_count_dirty;  // set by set_system_initialized(); tick() clears after 3 s
    uint32_t _init_ms;
    k_tid_t _main_thread;
    int     _argc;
    char *const *_argv;

    struct k_thread _timer_thread_data;
    struct k_thread _io_thread_data;

    struct UserThread {
        struct k_thread    thread_data;
        AP_HAL::MemberProc proc;
        bool               in_use;
    };
    UserThread _user_threads[MAX_USER_THREADS];

    static int _zephyr_priority(priority_base base, int8_t offset);
};
