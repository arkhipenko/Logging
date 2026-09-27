/* Minimal STM32 HAL and CMSIS stand-in for build tests (tests only).
 * On Arm the CMSIS intrinsics are real instructions. On the host they map to
 * the FreeRTOS POSIX port so the FreeRTOS guard can run in a simulator. */
#ifndef STUB_STM32F4XX_HAL_H
#define STUB_STM32F4XX_HAL_H
#include <stdint.h>

typedef enum { HAL_OK = 0, HAL_ERROR, HAL_BUSY, HAL_TIMEOUT } HAL_StatusTypeDef;
typedef struct { int instance; } UART_HandleTypeDef;
typedef struct { uint8_t Hours; uint8_t Minutes; uint8_t Seconds; } RTC_TimeTypeDef;
typedef struct { uint8_t Month; uint8_t Date; uint8_t Year; } RTC_DateTypeDef;
typedef struct { int instance; } RTC_HandleTypeDef;
#define RTC_FORMAT_BIN 0U
HAL_StatusTypeDef HAL_RTC_GetTime(RTC_HandleTypeDef *hrtc, RTC_TimeTypeDef *time, uint32_t format);
HAL_StatusTypeDef HAL_RTC_GetDate(RTC_HandleTypeDef *hrtc, RTC_DateTypeDef *date, uint32_t format);
uint32_t HAL_GetTick(void);
HAL_StatusTypeDef HAL_UART_Transmit(UART_HandleTypeDef *huart, uint8_t *data, uint16_t size, uint32_t timeout);

#if defined(__arm__)
static inline uint32_t __get_PRIMASK(void) { uint32_t r; __asm volatile ("MRS %0, primask" : "=r" (r)); return r; }
static inline void __set_PRIMASK(uint32_t v) { __asm volatile ("MSR primask, %0" : : "r" (v) : "memory"); }
static inline void __disable_irq(void) { __asm volatile ("cpsid i" : : : "memory"); }
static inline void __enable_irq(void) { __asm volatile ("cpsie i" : : : "memory"); }
static inline uint32_t __get_IPSR(void) { uint32_t r; __asm volatile ("MRS %0, ipsr" : "=r" (r)); return r; }
#else
void stub_irq_disable(void);
void stub_irq_restore(uint32_t state);
uint32_t stub_irq_state(void);
static inline uint32_t __get_PRIMASK(void) { return stub_irq_state(); }
static inline void __set_PRIMASK(uint32_t v) { stub_irq_restore(v); }
static inline void __disable_irq(void) { stub_irq_disable(); }
static inline void __enable_irq(void) { stub_irq_restore(0); }
static inline uint32_t __get_IPSR(void) { return 0; }
#endif

#endif
