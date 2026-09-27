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
#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"

// Maximum number of tasks inside the logging library at the same time
// (logging, or waiting for the lock). A call beyond this limit is dropped.
#ifndef LOGGING_MAX_TASKS
#define LOGGING_MAX_TASKS 8
#endif

static SemaphoreHandle_t log_mutex_handle = NULL;
#if (configSUPPORT_STATIC_ALLOCATION == 1)
static StaticSemaphore_t log_mutex_buffer;
#endif

// Recursion guard: tasks currently inside the logging library
static TaskHandle_t log_guard_task[LOGGING_MAX_TASKS];
static bool log_guard_used[LOGGING_MAX_TASKS];

/**
 * @brief Initialize the logging mutex (call from application startup)
 *
 * Call once, before the tasks that log are started.
 */
void log_platform_init(void) {
    if (log_mutex_handle == NULL) {
#if (configSUPPORT_STATIC_ALLOCATION == 1)
        log_mutex_handle = xSemaphoreCreateMutexStatic(&log_mutex_buffer);
#else
        log_mutex_handle = xSemaphoreCreateMutex();
#endif
    }
}

/**
 * @brief Delete the logging mutex
 */
void log_platform_deinit(void) {
    if (log_mutex_handle != NULL) {
        vSemaphoreDelete(log_mutex_handle);
        log_mutex_handle = NULL;
    }
}

/**
 * @brief Acquire logging lock with timeout (FreeRTOS implementation)
 *
 * @param timeout_ms Timeout in milliseconds
 * @return true if lock acquired, false on timeout
 */
bool log_lock(unsigned int timeout_ms) {
    if (log_mutex_handle == NULL) {
        return true;  // No mutex initialized, proceed without lock
    }
    return xSemaphoreTake(log_mutex_handle, pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
}

/**
 * @brief Release logging lock (FreeRTOS implementation)
 */
void log_unlock(void) {
    if (log_mutex_handle != NULL) {
        xSemaphoreGive(log_mutex_handle);
    }
}

/**
 * @brief Enter the logging library on this task
 *
 * Calls from interrupt context are refused: the mutex is not ISR safe.
 * The task table is protected by masking interrupts (PRIMASK), which also
 * works before the scheduler starts.
 *
 * @return false if this task is already inside, in an ISR, or the table is full
 */
bool log_guard_enter(void) {
    if (__get_IPSR() != 0U) {
        return false;
    }

    TaskHandle_t self = xTaskGetCurrentTaskHandle();
    int free_slot = -1;

    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    for (int i = 0; i < LOGGING_MAX_TASKS; i++) {
        if (log_guard_used[i]) {
            if (log_guard_task[i] == self) {
                __set_PRIMASK(primask);
                return false;
            }
        } else if (free_slot < 0) {
            free_slot = i;
        }
    }
    if (free_slot >= 0) {
        log_guard_used[free_slot] = true;
        log_guard_task[free_slot] = self;
    }
    __set_PRIMASK(primask);

    return free_slot >= 0;
}

/**
 * @brief Leave the logging library on this task
 */
void log_guard_exit(void) {
    TaskHandle_t self = xTaskGetCurrentTaskHandle();

    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    for (int i = 0; i < LOGGING_MAX_TASKS; i++) {
        if (log_guard_used[i] && log_guard_task[i] == self) {
            log_guard_used[i] = false;
            break;
        }
    }
    __set_PRIMASK(primask);
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
