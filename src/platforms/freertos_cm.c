/**
  ******************************************************************************
  * @file           : freertos_cm.c
  * @brief          : FreeRTOS platform for Arm Cortex-M targets without a
  *                   vendor port (nRF52, RP2040, SAMD51 and others)
  * @note           : Selected by logging_platform.h when FreeRTOS is on the
  *                   include path and the compiler targets an M-profile core
  *                   (unless LOGGING_FREERTOS_DISABLE). Provides the mutex,
  *                   the per-task recursion guard and a tick timestamp. No
  *                   vendor HAL: the core registers are read with inline
  *                   assembly.
  ******************************************************************************
  * @attention
  *
  * (C) 2025 Anatoli Arkhipenko
  * This code is provided as-is, without warranty of any kind.
  */

#include "../logging.h"
#include "../logging_platform.h"

#if defined(LOGGING_PLATFORM_FREERTOS_CM)

#include <stdint.h>

// *************************************************************************
//  Core Register Access
// *************************************************************************

#if defined(LOGGING_FREERTOS_CM_SIM)
// FreeRTOS POSIX simulator (tests only): the test program supplies these
uint32_t log_cm_sim_irq_state(void);
void log_cm_sim_irq_disable(void);
void log_cm_sim_irq_restore(uint32_t state);
#define LOG_CM_IN_ISR()         0
#define LOG_CM_IRQ_SAVE()       log_cm_sim_irq_state()
#define LOG_CM_IRQ_DISABLE()    log_cm_sim_irq_disable()
#define LOG_CM_IRQ_RESTORE(x)   log_cm_sim_irq_restore(x)
#else
static inline uint32_t log_cm_get_ipsr(void) {
    uint32_t r;
    __asm volatile ("MRS %0, ipsr" : "=r" (r));
    return r;
}
static inline uint32_t log_cm_get_primask(void) {
    uint32_t r;
    __asm volatile ("MRS %0, primask" : "=r" (r));
    return r;
}
static inline void log_cm_set_primask(uint32_t v) {
    __asm volatile ("MSR primask, %0" : : "r" (v) : "memory");
}
static inline void log_cm_disable_irq(void) {
    __asm volatile ("CPSID i" : : : "memory");
}
#define LOG_CM_IN_ISR()         (log_cm_get_ipsr() != 0U)
#define LOG_CM_IRQ_SAVE()       log_cm_get_primask()
#define LOG_CM_IRQ_DISABLE()    log_cm_disable_irq()
#define LOG_CM_IRQ_RESTORE(x)   log_cm_set_primask(x)
#endif

#include "freertos_common.h"

// *************************************************************************
//  Platform Hooks
// *************************************************************************

/**
 * @brief Create the logging mutex (call once before the tasks that log start)
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

bool log_lock(unsigned int timeout_ms) {
    return log_freertos_lock(timeout_ms);
}

void log_unlock(void) {
    log_freertos_unlock();
}

bool log_guard_enter(void) {
    return log_freertos_guard_enter();
}

void log_guard_exit(void) {
    log_freertos_guard_exit();
}

/**
 * @brief Timestamp in microseconds from the FreeRTOS tick count
 */
uint64_t log_get_timestamp_us(void) {
    return log_freertos_tick_us();
}

#endif // LOGGING_PLATFORM_FREERTOS_CM
