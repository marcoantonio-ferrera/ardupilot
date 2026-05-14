#include "Semaphores.h"

using namespace Zephyr;

Semaphore::Semaphore()
{
    k_mutex_init(&_mutex);
}

bool Semaphore::take(uint32_t timeout_ms)
{
    const k_timeout_t timeout = timeout_ms == HAL_SEMAPHORE_BLOCK_FOREVER ? K_FOREVER : K_MSEC(timeout_ms);
    return k_mutex_lock(&_mutex, timeout) == 0;
}

bool Semaphore::take_nonblocking()
{
    return k_mutex_lock(&_mutex, K_NO_WAIT) == 0;
}

bool Semaphore::give()
{
    return k_mutex_unlock(&_mutex) == 0;
}

BinarySemaphore::BinarySemaphore(bool initial_state)
{
    k_sem_init(&_sem, initial_state ? 1U : 0U, 1U);
}

bool BinarySemaphore::wait(uint32_t timeout_us)
{
    const k_timeout_t timeout = timeout_us == 0 ? K_NO_WAIT : K_USEC(timeout_us);
    return k_sem_take(&_sem, timeout) == 0;
}

bool BinarySemaphore::wait_blocking()
{
    return k_sem_take(&_sem, K_FOREVER) == 0;
}

void BinarySemaphore::signal()
{
    k_sem_give(&_sem);
}