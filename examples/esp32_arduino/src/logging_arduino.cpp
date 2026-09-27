/**
  ******************************************************************************
  * @file           : logging_arduino.cpp
  * @brief          : Arduino Serial backend driver for logging framework
  * @note           : Timestamp, lock and recursion guard come from the ESP32
  *                   port of the library (src/platforms/esp32.c)
  ******************************************************************************
  * @attention
  *
  * (C) 2025 Anatoli Arkhipenko
  * This code is provided as-is, without warranty of any kind.
  */

#include <Arduino.h>
#include "logging.h"
#include "logging_internal.h"

// *************************************************************************
//  Arduino Serial Backend Implementation
// *************************************************************************

/**
 * @brief Serial port wrapper for config
 *
 * Pass as cfg->config to select which Serial port to use:
 *   NULL or 0 = Serial (USB)
 *   1 = Serial1
 *   2 = Serial2
 */
static HardwareSerial* get_serial_port(void *config) {
    uintptr_t port = (uintptr_t)config;
    switch (port) {
        case 1:  return &Serial1;
        case 2:  return &Serial2;
        default: return &Serial;
    }
}

/**
 * @brief Initialize Arduino serial backend
 *
 * @param cfg Backend configuration
 * @return Serial port pointer
 */
static void *log_serial_arduino_init(log_backend_cfg_t *cfg) {
    return (void *)get_serial_port(cfg ? cfg->config : NULL);
}

/**
 * @brief Deinitialize Arduino serial backend
 */
static void log_serial_arduino_deinit(void *internal) {
    (void)internal;
}

/**
 * @brief Write log message to Arduino Serial
 */
static void log_serial_arduino_write(void *internal, const log_backend_cfg_t *cfg,
                                     unsigned int level, const char *tag,
                                     const char *file, int line,
                                     const char *timestamp_str, const char *message) {
    (void)cfg;

    HardwareSerial *serial = (HardwareSerial *)internal;
    if (!serial) {
        serial = &Serial;
    }

    // "[timestamp] LEVEL  [tag] [file:line] " then the message and CR LF
    char prefix[LOG_PREFIX_BUFFER_SIZE];
    log_format_prefix(prefix, sizeof(prefix), level, tag, file, line, timestamp_str);
    serial->print(prefix);
    serial->println(message);
}

// *************************************************************************
//  Arduino Serial Backend Driver Structure
// *************************************************************************

/**
 * @brief Arduino Serial backend driver
 *
 * Configuration:
 *   - cfg->config: Serial port selector
 *       NULL or 0 = Serial (USB)
 *       (void*)1 = Serial1
 *       (void*)2 = Serial2
 *
 * Example:
 *   static log_backend_cfg_t backends[] = {
 *       {
 *           .type = LOG_OUTPUT_CUSTOM,
 *           .driver = &log_driver_serial_arduino,
 *           .level = LOGGING_LEVEL_DEBUG,
 *           .enabled = true,
 *           .config = NULL,  // Use Serial (USB)
 *       },
 *   };
 */
extern "C" const log_backend_driver_t log_driver_serial_arduino = {
    .name   = "serial_arduino",
    .init   = log_serial_arduino_init,
    .deinit = log_serial_arduino_deinit,
    .write  = log_serial_arduino_write,
};
