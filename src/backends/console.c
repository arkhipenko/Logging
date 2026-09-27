/**
  ******************************************************************************
  * @file           : console.c
  * @brief          : Console output backend for logging framework
  * @note           : Outputs formatted log messages to stdout
  ******************************************************************************
  * @attention
  *
  * (C) 2025 Anatoli Arkhipenko
  * This code is provided as-is, without warranty of any kind.
  */

#include "../logging_internal.h"
#include <stdio.h>

// *************************************************************************
//  Console Backend Driver Functions
// *************************************************************************

/**
 * @brief Initialize console backend
 * @param cfg Backend configuration (unused for console)
 * @return Always NULL (console requires no internal state)
 */
void *log_console_driver_init(log_backend_cfg_t *cfg) {
    (void)cfg;  // Console requires no initialization
    return NULL;
}

/**
 * @brief Deinitialize console backend
 * @param internal Internal state (unused, always NULL)
 */
void log_console_driver_deinit(void *internal) {
    (void)internal;  // Console requires no cleanup
}

/**
 * @brief Write log message to console (stdout)
 *
 * Format: [timestamp] LEVEL  [tag] [file:line] message
 * Segments without data are omitted (see log_format_prefix).
 *
 * @param internal Internal state (unused)
 * @param cfg Backend configuration
 * @param level Log level (0-7)
 * @param tag Module tag (may be NULL)
 * @param file Source file (may be NULL)
 * @param line Source line
 * @param timestamp_str Formatted timestamp
 * @param message Formatted message
 */
void log_console_driver_write(void *internal, const log_backend_cfg_t *cfg,
                              unsigned int level, const char *tag,
                              const char *file, int line,
                              const char *timestamp_str, const char *message) {
    (void)internal;
    (void)cfg;

    char prefix[LOG_PREFIX_BUFFER_SIZE];
    log_format_prefix(prefix, sizeof(prefix), level, tag, file, line, timestamp_str);

    // fputs instead of printf: printf's stack frame is large on small targets
    fputs(prefix, stdout);
    fputs(message, stdout);
    fputc('\n', stdout);
    fflush(stdout);
}

// *************************************************************************
//  Console Backend Driver Structure
// *************************************************************************

/**
 * @brief Console backend driver
 *
 * Provides callbacks for console output. Can be referenced directly
 * when configuring backends, or is used automatically when
 * type == LOG_OUTPUT_CONSOLE and driver == NULL.
 */
const log_backend_driver_t log_driver_console = {
    .name   = "console",
    .init   = log_console_driver_init,
    .deinit = log_console_driver_deinit,
    .write  = log_console_driver_write,
};
