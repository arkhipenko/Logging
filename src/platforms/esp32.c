/**
  ******************************************************************************
  * @file           : esp32.c
  * @brief          : ESP32 platform implementation for logging framework
  * @note           : Provides timestamp, thread-safe locking and recursion
  *                   guard for ESP32/ESP-IDF (also Arduino-ESP32)
  ******************************************************************************
  * @attention
  *
  * (C) 2025 Anatoli Arkhipenko
  * This code is provided as-is, without warranty of any kind.
  */

#include "../logging.h"
#include "../logging_platform.h"

#if defined(LOGGING_PLATFORM_ESP32)

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_timer.h"

// *************************************************************************
//  Static Variables
// *************************************************************************

static SemaphoreHandle_t log_mutex_handle = NULL;

// Recursion guard: ESP-IDF gives every FreeRTOS task its own thread-local storage
static __thread int log_guard_flag = 0;

// *************************************************************************
//  Platform Initialization
// *************************************************************************

/**
 * @brief Initialize the logging mutex (call from application startup)
 *
 * This should be called early in app_main() before any logging calls.
 * If not called, logging will proceed without thread safety.
 */
void log_platform_init(void) {
    if (log_mutex_handle == NULL) {
        log_mutex_handle = xSemaphoreCreateMutex();
    }
}

/**
 * @brief Deinitialize platform resources
 */
void log_platform_deinit(void) {
    if (log_mutex_handle != NULL) {
        vSemaphoreDelete(log_mutex_handle);
        log_mutex_handle = NULL;
    }
}

// *************************************************************************
//  Platform Functions
// *************************************************************************

/**
 * @brief Get current timestamp in microseconds (ESP32 implementation)
 *
 * Uses esp_timer_get_time() which provides microsecond resolution
 * monotonic time since boot.
 *
 * @return Timestamp in microseconds
 */
uint64_t log_get_timestamp_us(void) {
    return (uint64_t)esp_timer_get_time();
}

/**
 * @brief Acquire logging lock with timeout (ESP32 implementation)
 *
 * Uses FreeRTOS mutex with timeout.
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
 * @brief Release logging lock (ESP32 implementation)
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
 *
 * @return false if this task is already inside a log call, or in an ISR
 */
bool log_guard_enter(void) {
    if (xPortInIsrContext() || log_guard_flag) {
        return false;
    }
    log_guard_flag = 1;
    return true;
}

/**
 * @brief Leave the logging library on this task
 */
void log_guard_exit(void) {
    log_guard_flag = 0;
}

#endif // LOGGING_PLATFORM_ESP32
