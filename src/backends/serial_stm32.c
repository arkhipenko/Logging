/**
  ******************************************************************************
  * @file           : serial_stm32.c
  * @brief          : STM32 UART/Serial console backend for logging framework
  * @note           : Outputs formatted log messages via STM32 HAL UART
  ******************************************************************************
  * @attention
  *
  * (C) 2025 Anatoli Arkhipenko
  * This code is provided as-is, without warranty of any kind.
  */

#include "../logging_internal.h"

#if defined(LOGGING_TARGET_STM32)

#include <stdio.h>
#include <string.h>

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
//  Configuration
// *************************************************************************

#ifndef LOG_STM32_TX_TIMEOUT
#define LOG_STM32_TX_TIMEOUT 100  // UART transmit timeout in ms
#endif

#ifndef LOG_STM32_LINE_BUFFER_SIZE
#define LOG_STM32_LINE_BUFFER_SIZE 256
#endif

// *************************************************************************
//  Serial Backend Driver Functions
// *************************************************************************

/**
 * @brief Initialize STM32 serial backend
 *
 * @param cfg Backend configuration (config field = UART_HandleTypeDef*)
 * @return The UART handle pointer for internal use
 */
void *log_serial_stm32_driver_init(log_backend_cfg_t *cfg) {
    if (!cfg || !cfg->config) {
        return NULL;
    }
    // config should point to an initialized UART_HandleTypeDef
    return cfg->config;
}

/**
 * @brief Deinitialize STM32 serial backend
 *
 * @param internal UART handle (we don't own it, so no cleanup needed)
 */
void log_serial_stm32_driver_deinit(void *internal) {
    (void)internal;  // UART is managed by application
}

/**
 * @brief Write log message to STM32 UART
 *
 * @param internal UART_HandleTypeDef* handle
 * @param cfg Backend configuration
 * @param level Log level (0-7)
 * @param tag Module tag (may be NULL)
 * @param file Source file (may be NULL)
 * @param line Source line
 * @param timestamp_str Formatted timestamp
 * @param message Formatted message
 */
void log_serial_stm32_driver_write(void *internal, const log_backend_cfg_t *cfg,
                                   unsigned int level, const char *tag,
                                   const char *file, int line,
                                   const char *timestamp_str, const char *message) {
    (void)cfg;

    UART_HandleTypeDef *huart = (UART_HandleTypeDef *)internal;
    if (!huart) {
        return;
    }

    char line_buffer[LOG_STM32_LINE_BUFFER_SIZE];

    // A truncated line keeps its CR LF (see log_format_line)
    size_t len = log_format_line(line_buffer, sizeof(line_buffer),
                                 level, tag, file, line,
                                 timestamp_str, message, "\r\n");
    if (len > 0) {
        HAL_UART_Transmit(huart, (uint8_t *)line_buffer, (uint16_t)len, LOG_STM32_TX_TIMEOUT);
    }
}

// *************************************************************************
//  Serial Backend Driver Structure
// *************************************************************************

/**
 * @brief STM32 Serial backend driver
 *
 * Configuration:
 *   - cfg->config: (UART_HandleTypeDef *) pointer to initialized UART handle
 *
 * Example usage:
 *   extern UART_HandleTypeDef huart2;
 *   static log_backend_cfg_t backends[] = {
 *       {
 *           .type = LOG_OUTPUT_CUSTOM,
 *           .driver = &log_driver_serial_stm32,
 *           .level = LOGGING_LEVEL_DEBUG,
 *           .enabled = true,
 *           .config = &huart2,
 *       },
 *   };
 */
const log_backend_driver_t log_driver_serial_stm32 = {
    .name   = "serial_stm32",
    .init   = log_serial_stm32_driver_init,
    .deinit = log_serial_stm32_driver_deinit,
    .write  = log_serial_stm32_driver_write,
};

#endif // LOGGING_TARGET_STM32
