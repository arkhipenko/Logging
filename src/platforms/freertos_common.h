/**
  ******************************************************************************
  * @file           : freertos_common.h
  * @brief          : FreeRTOS lock and per-task recursion guard shared by the
  *                   STM32 and the FreeRTOS Cortex-M platforms
  * @note           : Private header. Included by exactly one platform source
  *                   per build, after that source defined the four register
  *                   accessors below. Not for application code.
  ******************************************************************************
  * @attention
  *
  * (C) 2025 Anatoli Arkhipenko
  * This code is provided as-is, without warranty of any kind.
  */

#ifndef __LOGGING_FREERTOS_COMMON_H
#define __LOGGING_FREERTOS_COMMON_H

// The including platform source defines, before this include:
//   LOG_CM_IN_ISR()         nonzero in handler (interrupt) mode
//   LOG_CM_IRQ_SAVE()       current interrupt mask (PRIMASK)
//   LOG_CM_IRQ_DISABLE()    mask interrupts
//   LOG_CM_IRQ_RESTORE(x)   restore the mask returned by LOG_CM_IRQ_SAVE()
#if !defined(LOG_CM_IN_ISR) || !defined(LOG_CM_IRQ_SAVE) || \
    !defined(LOG_CM_IRQ_DISABLE) || !defined(LOG_CM_IRQ_RESTORE)
#error "freertos_common.h: define the LOG_CM_* register accessors before including"
#endif

#if defined(LOGGING_FREERTOS_INCLUDE_SUBDIR)
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#else
#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"
#endif

#include <stdbool.h>
#include <stdint.h>

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
 * @brief Create the logging mutex once (call before the tasks that log start)
 */
static inline void log_freertos_init(void) {
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
static inline void log_freertos_deinit(void) {
    if (log_mutex_handle != NULL) {
        vSemaphoreDelete(log_mutex_handle);
        log_mutex_handle = NULL;
    }
}

/**
 * @brief Acquire the logging lock with a timeout
 *
 * @param timeout_ms Timeout in milliseconds
 * @return true if the lock was acquired, or no mutex exists yet; false on timeout
 */
static inline bool log_freertos_lock(unsigned int timeout_ms) {
    if (log_mutex_handle == NULL) {
        return true;  // No mutex initialized, proceed without lock
    }
    return xSemaphoreTake(log_mutex_handle, pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
}

/**
 * @brief Release the logging lock
 */
static inline void log_freertos_unlock(void) {
    if (log_mutex_handle != NULL) {
        xSemaphoreGive(log_mutex_handle);
    }
}

/**
 * @brief Enter the logging library on this task
 *
 * Calls from interrupt context are refused: the mutex is not ISR safe.
 * The task table is protected by masking interrupts, which also works
 * before the scheduler starts.
 *
 * @return false if this task is already inside, in an ISR, or the table is full
 */
static inline bool log_freertos_guard_enter(void) {
    if (LOG_CM_IN_ISR()) {
        return false;
    }

    TaskHandle_t self = xTaskGetCurrentTaskHandle();
    int free_slot = -1;

    uint32_t mask = LOG_CM_IRQ_SAVE();
    LOG_CM_IRQ_DISABLE();
    for (int i = 0; i < LOGGING_MAX_TASKS; i++) {
        if (log_guard_used[i]) {
            if (log_guard_task[i] == self) {
                LOG_CM_IRQ_RESTORE(mask);
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
    LOG_CM_IRQ_RESTORE(mask);

    return free_slot >= 0;
}

/**
 * @brief Leave the logging library on this task
 */
static inline void log_freertos_guard_exit(void) {
    TaskHandle_t self = xTaskGetCurrentTaskHandle();

    uint32_t mask = LOG_CM_IRQ_SAVE();
    LOG_CM_IRQ_DISABLE();
    for (int i = 0; i < LOGGING_MAX_TASKS; i++) {
        if (log_guard_used[i] && log_guard_task[i] == self) {
            log_guard_used[i] = false;
            break;
        }
    }
    LOG_CM_IRQ_RESTORE(mask);
}

/**
 * @brief Monotonic time from the FreeRTOS tick count, in microseconds
 *
 * Resolution is one tick (configTICK_RATE_HZ). The count does not advance
 * before the scheduler starts, and wraps with TickType_t.
 */
static inline uint64_t log_freertos_tick_us(void) {
    return (uint64_t)xTaskGetTickCount() * 1000000ULL / (uint64_t)configTICK_RATE_HZ;
}

#endif /* __LOGGING_FREERTOS_COMMON_H */
