/**
  ******************************************************************************
  * @file           : linux.c
  * @brief          : POSIX platform implementation for logging framework
  * @note           : Provides timestamp, thread-safe locking and recursion
  *                   guard for Linux, other Unix systems and macOS
  ******************************************************************************
  * @attention
  *
  * (C) 2025 Anatoli Arkhipenko
  * This code is provided as-is, without warranty of any kind.
  */

// Feature test macros must come before any includes
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200112L
#endif

#include "../logging.h"
#include "../logging_platform.h"

#if defined(LOGGING_PLATFORM_POSIX)

#include <time.h>
#include <pthread.h>
#include <errno.h>

// *************************************************************************
//  Static Variables
// *************************************************************************

static pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;

// Recursion guard: one flag per thread
static __thread int log_guard_flag = 0;

// *************************************************************************
//  Platform Functions
// *************************************************************************

/**
 * @brief Get current timestamp in microseconds (POSIX implementation)
 *
 * Uses CLOCK_MONOTONIC for monotonic time that doesn't jump on system
 * time changes.
 *
 * @return Timestamp in microseconds
 */
uint64_t log_get_timestamp_us(void) {
    struct timespec ts;

    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return 0;
    }

    return (uint64_t)ts.tv_sec * 1000000ULL + (uint64_t)ts.tv_nsec / 1000ULL;
}

#if defined(__APPLE__) || defined(LOGGING_POSIX_NO_TIMEDLOCK)

/**
 * @brief Acquire logging lock with timeout (polling implementation)
 *
 * macOS has no pthread_mutex_timedlock(). The lock is polled with
 * pthread_mutex_trylock() once per millisecond until the timeout.
 *
 * @param timeout_ms Timeout in milliseconds
 * @return true if lock acquired, false on timeout
 */
bool log_lock(unsigned int timeout_ms) {
    for (unsigned int waited_ms = 0; ; waited_ms++) {
        if (pthread_mutex_trylock(&log_mutex) == 0) {
            return true;
        }
        if (waited_ms >= timeout_ms) {
            return false;
        }
        struct timespec pause = { 0, 1000000L };  // 1 ms
        nanosleep(&pause, NULL);
    }
}

#else

/**
 * @brief Acquire logging lock with timeout (POSIX implementation)
 *
 * Uses pthread mutex with timeout.
 *
 * @param timeout_ms Timeout in milliseconds
 * @return true if lock acquired, false on timeout
 */
bool log_lock(unsigned int timeout_ms) {
    struct timespec abs_timeout;

    // Get current time
    if (clock_gettime(CLOCK_REALTIME, &abs_timeout) != 0) {
        // Fallback: try immediate lock
        return pthread_mutex_trylock(&log_mutex) == 0;
    }

    // Calculate absolute timeout
    uint64_t timeout_ns = (uint64_t)timeout_ms * 1000000ULL;
    abs_timeout.tv_sec += timeout_ns / 1000000000ULL;
    abs_timeout.tv_nsec += timeout_ns % 1000000000ULL;

    // Handle nanosecond overflow
    if (abs_timeout.tv_nsec >= 1000000000L) {
        abs_timeout.tv_sec += 1;
        abs_timeout.tv_nsec -= 1000000000L;
    }

    // Try to lock with timeout
    int result = pthread_mutex_timedlock(&log_mutex, &abs_timeout);

    return (result == 0);
}

#endif // __APPLE__ || LOGGING_POSIX_NO_TIMEDLOCK

/**
 * @brief Release logging lock (POSIX implementation)
 */
void log_unlock(void) {
    pthread_mutex_unlock(&log_mutex);
}

/**
 * @brief Platform initialization (POSIX - mutex is statically initialized)
 */
void log_platform_init(void) {
    // No-op
}

/**
 * @brief Platform deinitialization (POSIX - nothing to release)
 */
void log_platform_deinit(void) {
    // No-op
}

/**
 * @brief Enter the logging library on this thread
 *
 * @return false if this thread is already inside a log call
 */
bool log_guard_enter(void) {
    if (log_guard_flag) {
        return false;
    }
    log_guard_flag = 1;
    return true;
}

/**
 * @brief Leave the logging library on this thread
 */
void log_guard_exit(void) {
    log_guard_flag = 0;
}

#endif // LOGGING_PLATFORM_POSIX
