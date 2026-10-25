/**
  ******************************************************************************
  * @file           : stm32.c
  * @brief          : STM32 platform implementation for logging framework
  * @note           : Provides timestamp, thread-safe locking and recursion
  *                   guard for STM32. Supports both bare-metal and FreeRTOS
  *                   configurations (see logging_platform.h for detection)
  ******************************************************************************
  * @attention
  *
  * (C) 2025 Anatoli Arkhipenko
  * This code is provided as-is, without warranty of any kind.
  */

#include "../logging.h"
#include "../logging_platform.h"

#if defined(LOGGING_PLATFORM_STM32)

// Include HAL header based on the specific STM32 family
#if defined(STM32F0)
#include "stm32f0xx_hal.h"
#elif defined(STM32F1)
#include "stm32f1xx_hal.h"
#elif defined(STM32F2)
#include "stm32f2xx_hal.h"
#elif defined(STM32F3)
#include "stm32f3xx_hal.h"
#elif defined(STM32F4)
#include "stm32f4xx_hal.h"
#elif defined(STM32F7)
#include "stm32f7xx_hal.h"
#elif defined(STM32G0)
#include "stm32g0xx_hal.h"
#elif defined(STM32G4)
#include "stm32g4xx_hal.h"
#elif defined(STM32H7)
#include "stm32h7xx_hal.h"
#elif defined(STM32L0)
#include "stm32l0xx_hal.h"
#elif defined(STM32L1)
#include "stm32l1xx_hal.h"
#elif defined(STM32L4)
#include "stm32l4xx_hal.h"
#elif defined(STM32L5)
#include "stm32l5xx_hal.h"
#elif defined(STM32U5)
#include "stm32u5xx_hal.h"
#elif defined(STM32WB)
#include "stm32wbxx_hal.h"
#elif defined(STM32WL)
#include "stm32wlxx_hal.h"
#endif

// *************************************************************************
//  FreeRTOS Support (optional)
// *************************************************************************

#if defined(LOGGING_STM32_USE_FREERTOS)
// The lock and the per-task guard live in freertos_common.h (shared with the
// FreeRTOS Cortex-M platform). The CMSIS intrinsics supply the registers.
#define LOG_CM_IN_ISR()         (__get_IPSR() != 0U)
#define LOG_CM_IRQ_SAVE()       __get_PRIMASK()
#define LOG_CM_IRQ_DISABLE()    __disable_irq()
#define LOG_CM_IRQ_RESTORE(x)   __set_PRIMASK(x)
#include "freertos_common.h"

/**
 * @brief Initialize the logging mutex (call from application startup)
 *
 * Call once, before the tasks that log are started.
 */
void log_platform_init(void) {
    log_freertos_init();
}

/**
 * @brief Delete the logging mutex
 */
void log_platform_deinit(void) {
    log_freertos_deinit();
}

/**
 * @brief Acquire logging lock with timeout (FreeRTOS implementation)
 */
bool log_lock(unsigned int timeout_ms) {
    return log_freertos_lock(timeout_ms);
}

/**
 * @brief Release logging lock (FreeRTOS implementation)
 */
void log_unlock(void) {
    log_freertos_unlock();
}

/**
 * @brief Enter the logging library on this task (false in an ISR, when
 *        already inside, or when LOGGING_MAX_TASKS tasks are inside)
 */
bool log_guard_enter(void) {
    return log_freertos_guard_enter();
}

/**
 * @brief Leave the logging library on this task
 */
void log_guard_exit(void) {
    log_freertos_guard_exit();
}

#else
// Bare-metal: simple interrupt-based locking

static volatile bool log_lock_flag = false;

// Recursion guard: one flag for the main loop and all interrupts. An
// interrupt that logs while the main loop is inside a log call is dropped.
static volatile int log_guard_flag = 0;

/**
 * @brief Platform initialization (bare metal - nothing to create)
 */
void log_platform_init(void) {
    // No-op
}

/**
 * @brief Platform deinitialization (bare metal - nothing to release)
 */
void log_platform_deinit(void) {
    // No-op
}

/**
 * @brief Acquire logging lock with timeout (bare-metal implementation)
 *
 * Uses interrupt disable for critical section protection.
 * Timeout is approximate since we don't have an RTOS scheduler.
 *
 * @param timeout_ms Timeout in milliseconds (approximate)
 * @return true if lock acquired, false on timeout
 */
bool log_lock(unsigned int timeout_ms) {
    uint32_t start = HAL_GetTick();

    while (log_lock_flag) {
        if ((HAL_GetTick() - start) >= timeout_ms) {
            return false;
        }
    }

    __disable_irq();
    if (log_lock_flag) {
        __enable_irq();
        return false;
    }
    log_lock_flag = true;
    __enable_irq();

    return true;
}

/**
 * @brief Release logging lock (bare-metal implementation)
 */
void log_unlock(void) {
    log_lock_flag = false;
}

/**
 * @brief Enter the logging library
 *
 * @return false if a log call is already in progress
 */
bool log_guard_enter(void) {
    if (log_guard_flag) {
        return false;
    }
    log_guard_flag = 1;
    return true;
}

/**
 * @brief Leave the logging library
 */
void log_guard_exit(void) {
    log_guard_flag = 0;
}

#endif // LOGGING_STM32_USE_FREERTOS

// *************************************************************************
//  Timestamp Functions
// *************************************************************************

/**
 * @brief Get current timestamp in microseconds (STM32 implementation)
 *
 * Uses HAL_GetTick() which provides milliseconds. For microsecond
 * resolution, consider using a hardware timer or DWT cycle counter.
 *
 * @return Timestamp in microseconds
 */
uint64_t log_get_timestamp_us(void) {
    // HAL_GetTick() returns milliseconds, convert to microseconds
    return (uint64_t)HAL_GetTick() * 1000ULL;
}

#endif // LOGGING_PLATFORM_STM32
