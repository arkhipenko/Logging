/**
  ******************************************************************************
  * @file           : logging_internal.h
  * @brief          : Internal structures and functions for logging framework
  * @note           : Private header - not for external use
  ******************************************************************************
  * @attention
  *
  * (C) 2025 Anatoli Arkhipenko
  * This code is provided as-is, without warranty of any kind.
  */

#ifndef __LOGGING_INTERNAL_H
#define __LOGGING_INTERNAL_H

#include "logging.h"
#include "logging_platform.h"
#include <stdio.h>
#include <string.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

// *************************************************************************
//  Internal Constants
// *************************************************************************

// Message buffer. It is static (one for the whole program) and used only
// while the logging lock is held, so its size costs RAM, not task stack.
// Longer messages are truncated.
#ifndef LOG_MESSAGE_BUFFER_SIZE
#define LOG_MESSAGE_BUFFER_SIZE 1024
#endif

// Keeps a helper out of its caller's stack frame
#if defined(__GNUC__) || defined(__clang__)
#define LOG_NOINLINE __attribute__((noinline))
#else
#define LOG_NOINLINE
#endif

// Stack buffer for a call that proceeds without the lock (lock timeout)
#ifndef LOGGING_UNLOCKED_BUFFER_SIZE
#define LOGGING_UNLOCKED_BUFFER_SIZE 128
#endif

// Line prefix buffer used by the console and file drivers:
// "[timestamp] LEVEL  [tag] [file:line] " - longer prefixes are truncated
#ifndef LOG_PREFIX_BUFFER_SIZE
#define LOG_PREFIX_BUFFER_SIZE 128
#endif

// *************************************************************************
//  Global State Structure
// *************************************************************************

typedef struct {
    log_backend_cfg_t *backends;             // Pointer to backend array
    size_t backend_count;                    // Number of backends
    bool initialized;
    volatile bool busy;                      // log_init/log_deinit in progress
    volatile int max_level;                  // Highest level any backend accepts (-1 = none)
    log_timestamp_type_t timestamp_type;     // Current timestamp format
    log_timestamp_formatter_t custom_formatter; // Custom formatter callback
} log_state_t;

// *************************************************************************
//  Global State
// *************************************************************************

extern log_state_t g_log_state;

// *************************************************************************
//  Backend Driver Resolution
// *************************************************************************

/**
 * @brief Get driver for backend configuration
 *
 * Returns the driver pointer if set, otherwise looks up built-in driver
 * based on the type field.
 *
 * @param cfg Backend configuration
 * @return Driver pointer, or NULL if not found
 */
const log_backend_driver_t *log_get_driver(const log_backend_cfg_t *cfg);

// *************************************************************************
//  Backend Driver Implementations (internal)
// *************************************************************************

// --- Console Backend Driver Functions ---
void *log_console_driver_init(log_backend_cfg_t *cfg);
void log_console_driver_deinit(void *internal);
void log_console_driver_write(void *internal, const log_backend_cfg_t *cfg,
                              unsigned int level, const char *tag,
                              const char *file, int line,
                              const char *timestamp_str, const char *message);

// --- File Backend Driver Functions ---
void *log_file_driver_init(log_backend_cfg_t *cfg);
void log_file_driver_deinit(void *internal);
void log_file_driver_write(void *internal, const log_backend_cfg_t *cfg,
                           unsigned int level, const char *tag,
                           const char *file, int line,
                           const char *timestamp_str, const char *message);

// *************************************************************************
//  Timestamp Formatting Functions
// *************************************************************************

/**
 * @brief Clock readings shared by all backends of one log call
 *
 * The wall clock is read at most once per call, on first use.
 */
typedef struct {
    uint64_t mono_us;       // log_get_timestamp_us() value
    bool wall_read;         // wall clock already read
    bool wall_ok;           // wall clock valid
    struct tm wall_tm;      // local time
    int wall_ms;            // milliseconds part
} log_ts_ctx_t;

/**
 * @brief Format timestamp with specific format type
 *
 * Always writes a terminated string (possibly empty) when buffer_size > 0.
 *
 * @param type Timestamp format type
 * @param custom_formatter Custom formatter (used if type == LOG_TS_CUSTOM)
 * @param ctx Clock readings for this log call
 * @param buffer Output buffer
 * @param buffer_size Size of output buffer
 * @return Number of characters written
 */
int log_format_timestamp_ex(log_timestamp_type_t type,
                            log_timestamp_formatter_t custom_formatter,
                            log_ts_ctx_t *ctx,
                            char *buffer, size_t buffer_size);

// *************************************************************************
//  Output Line Formatting (shared by all built-in drivers)
// *************************************************************************

/**
 * @brief Build the line prefix "[timestamp] LEVEL  [tag] [file:line] "
 *
 * Segments are omitted when the timestamp is empty, the tag is NULL, or
 * file is NULL or line <= 0. The result is truncated to buffer_size - 1.
 *
 * @return Length of the prefix written (excluding terminator)
 */
size_t log_format_prefix(char *buffer, size_t buffer_size,
                         unsigned int level, const char *tag,
                         const char *file, int line,
                         const char *timestamp_str);

/**
 * @brief Build a complete output line: prefix, message and eol
 *
 * When the line does not fit, the message is truncated and eol is still
 * placed at the end, so a truncated line keeps its terminator.
 *
 * @param eol Line terminator ("\n" or "\r\n")
 * @return Length of the line written (excluding terminator), 0 if the
 *         buffer cannot hold eol
 */
size_t log_format_line(char *buffer, size_t buffer_size,
                       unsigned int level, const char *tag,
                       const char *file, int line,
                       const char *timestamp_str, const char *message,
                       const char *eol);

/**
 * @brief Determine if file/line should be shown for given mode
 *
 * @param mode File/line display mode
 * @return true if file/line should be displayed
 */
bool log_should_show_file_line(log_file_line_mode_t mode);

// *************************************************************************
//  Helper Functions
// *************************************************************************

/**
 * @brief Get level character for display
 *
 * @param level Log level (0-7)
 * @return Single character representing level
 */
static inline char log_get_level_char(unsigned int level) {
    static const char level_chars[] = "EACEWNID";
    if (level > LOGGING_LEVEL_MAX) {
        level = LOGGING_LEVEL_MAX;
    }
    return level_chars[level];
}

/**
 * @brief Get level string for display
 *
 * @param level Log level (0-7)
 * @return String representing level
 */
static inline const char* log_get_level_string(unsigned int level) {
    static const char* level_strings[] = {
        "EMERG", "ALERT", "CRIT", "ERR",
        "WARN", "NOTICE", "INFO", "DEBUG"
    };
    if (level > LOGGING_LEVEL_MAX) {
        level = LOGGING_LEVEL_MAX;
    }
    return level_strings[level];
}

/**
 * @brief Extract filename from full path
 *
 * @param path Full file path
 * @return Pointer to filename portion
 */
static inline const char* log_extract_filename(const char *path) {
    if (!path) return NULL;

    const char *slash = strrchr(path, '/');
    if (!slash) {
        slash = strrchr(path, '\\');
    }
    return slash ? (slash + 1) : path;
}

#ifdef __cplusplus
}
#endif

#endif /* __LOGGING_INTERNAL_H */
