/**
  ******************************************************************************
  * @file           : serial_esp32.c
  * @brief          : ESP32 Serial console backend for logging framework
  * @note           : Outputs formatted log messages via ESP32 UART
  ******************************************************************************
  * @attention
  *
  * (C) 2025 Anatoli Arkhipenko
  * This code is provided as-is, without warranty of any kind.
  */

#include "../logging_internal.h"

#if defined(LOGGING_TARGET_ESP32)

#include <stdio.h>
#include <string.h>
#include "driver/uart.h"

// *************************************************************************
//  Configuration
// *************************************************************************

#ifndef LOG_ESP32_LINE_BUFFER_SIZE
#define LOG_ESP32_LINE_BUFFER_SIZE 256
#endif

// *************************************************************************
//  Serial Backend Driver Functions
// *************************************************************************

/**
 * @brief Initialize ESP32 serial backend
 *
 * @param cfg Backend configuration (config field = uart_port_t* or NULL for UART_NUM_0)
 * @return The UART port number cast to pointer
 */
void *log_serial_esp32_driver_init(log_backend_cfg_t *cfg) {
    if (!cfg) {
        return NULL;
    }

    // If config is NULL, use default UART (typically UART_NUM_0 for USB/Serial)
    // If config is provided, it should be a uart_port_t value cast to void*
    uart_port_t port = cfg->config ? (uart_port_t)(uintptr_t)cfg->config : UART_NUM_0;

    // Return the port number as internal state
    return (void *)(uintptr_t)port;
}

/**
 * @brief Deinitialize ESP32 serial backend
 *
 * @param internal UART port (we don't own it, so no cleanup needed)
 */
void log_serial_esp32_driver_deinit(void *internal) {
    (void)internal;  // UART is managed by ESP-IDF
}

/**
 * @brief Write log message to ESP32 UART
 *
 * @param internal UART port number
 * @param cfg Backend configuration
 * @param level Log level (0-7)
 * @param tag Module tag (may be NULL)
 * @param file Source file (may be NULL)
 * @param line Source line
 * @param timestamp_str Formatted timestamp
 * @param message Formatted message
 */
void log_serial_esp32_driver_write(void *internal, const log_backend_cfg_t *cfg,
                                   unsigned int level, const char *tag,
                                   const char *file, int line,
                                   const char *timestamp_str, const char *message) {
    (void)cfg;

    uart_port_t port = (uart_port_t)(uintptr_t)internal;

    char line_buffer[LOG_ESP32_LINE_BUFFER_SIZE];

    // A truncated line keeps its CR LF (see log_format_line)
    size_t len = log_format_line(line_buffer, sizeof(line_buffer),
                                 level, tag, file, line,
                                 timestamp_str, message, "\r\n");
    if (len > 0) {
        uart_write_bytes(port, line_buffer, len);
    }
}

// *************************************************************************
//  Serial Backend Driver Structure
// *************************************************************************

/**
 * @brief ESP32 Serial backend driver
 *
 * Configuration:
 *   - cfg->config: (uart_port_t) UART port number cast to void*
 *                  NULL or 0 = UART_NUM_0 (default, USB Serial)
 *                  (void*)1 = UART_NUM_1
 *                  (void*)2 = UART_NUM_2
 *
 * The ESP-IDF UART driver must be installed on the port before logging
 * (uart_driver_install(); Arduino Serial.begin() does this).
 *
 * Example usage:
 *   static log_backend_cfg_t backends[] = {
 *       {
 *           .type = LOG_OUTPUT_CUSTOM,
 *           .driver = &log_driver_serial_esp32,
 *           .level = LOGGING_LEVEL_DEBUG,
 *           .enabled = true,
 *           .config = (void*)UART_NUM_0,  // or NULL for default
 *       },
 *   };
 */
const log_backend_driver_t log_driver_serial_esp32 = {
    .name   = "serial_esp32",
    .init   = log_serial_esp32_driver_init,
    .deinit = log_serial_esp32_driver_deinit,
    .write  = log_serial_esp32_driver_write,
};

#endif // LOGGING_TARGET_ESP32
