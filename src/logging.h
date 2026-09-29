/**
  ******************************************************************************
  * @file           : logging.h
  * @brief          : Multi-Backend Logging Framework
  * @note           : Publish/subscribe logging framework supporting multiple
  *                   simultaneous backends with per-backend level filtering.
  ******************************************************************************
  * @attention
  *
  * (C) 2025 Anatoli Arkhipenko
  * This code is provided as-is, without warranty of any kind.
  */

#ifndef __LOGGING_H
#define __LOGGING_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>
#include <stdarg.h>
#include <stddef.h>

// *************************************************************************
//  Compile-time Configuration
// *************************************************************************

// Uncomment to fully disable logging and reduce code size
// #define LOGGING_DISABLE_LOGGING

// Uncomment to enable file/line information in logs
// #define LOGGING_ENABLE_FILE_LINE

// Most verbose level compiled into the binary: a number or a LOGGING_LEVEL_*
// name, for example -DLOGGING_MAX_COMPILED_LEVEL=LOGGING_LEVEL_INFO.
// Calls above it leave no code and no strings. Backend levels set at run
// time select only among the compiled levels. LOGGING_LEVEL_NONE removes all.
#ifndef LOGGING_MAX_COMPILED_LEVEL
#define LOGGING_MAX_COMPILED_LEVEL LOGGING_LEVEL_DEBUG
#endif

// Lock timeout in milliseconds
#ifndef LOGGING_LOCK_TIMEOUT_MS
#define LOGGING_LOCK_TIMEOUT_MS 200
#endif

// Kept for source compatibility only. The library does not use it:
// log_init() accepts an array of any length.
#ifndef LOGGING_MAX_BACKENDS
#define LOGGING_MAX_BACKENDS 4
#endif

// Maximum timestamp string length
#ifndef LOGGING_TIMESTAMP_BUFFER_SIZE
#define LOGGING_TIMESTAMP_BUFFER_SIZE 32
#endif

// *************************************************************************
//  Log Levels (Syslog Compatible)
// *************************************************************************

// Only prefixed names here: NimBLE, ArduinoLog and <syslog.h> define
// LOG_LEVEL_* and LOG_ERR-style names with other values. The short names
// LOG_LEVEL_* and LOG_EMERG() .. LOG_DEBUG() are in logging_short.h.
#define LOGGING_LEVEL_NONE     (-1) // Disable output for this subscriber
#define LOGGING_LEVEL_EMERG    0    // System unusable
#define LOGGING_LEVEL_ALERT    1    // Action must be taken immediately
#define LOGGING_LEVEL_CRIT     2    // Critical conditions
#define LOGGING_LEVEL_ERR      3    // Error conditions
#define LOGGING_LEVEL_WARNING  4    // Warning conditions
#define LOGGING_LEVEL_NOTICE   5    // Normal but significant
#define LOGGING_LEVEL_INFO     6    // Informational
#define LOGGING_LEVEL_DEBUG    7    // Debug-level messages
#define LOGGING_LEVEL_MAX      LOGGING_LEVEL_DEBUG

// *************************************************************************
//  Backend Types
// *************************************************************************

typedef enum {
    LOG_OUTPUT_NONE = 0,
    LOG_OUTPUT_CONSOLE,      // stdout
    LOG_OUTPUT_FILE,         // File output
    LOG_OUTPUT_SYSLOG,       // Linux syslog (not implemented in this build)
    LOG_OUTPUT_JOURNALD,     // systemd journald (not implemented in this build)
    LOG_OUTPUT_UART,         // UART/serial (not implemented in this build)
    LOG_OUTPUT_FLASH,        // Flash memory (not implemented in this build)
    LOG_OUTPUT_NETWORK,      // Network socket (not implemented in this build)
    LOG_OUTPUT_CUSTOM        // User-provided callback
} log_output_type_t;

// *************************************************************************
//  Timestamp Formatting
// *************************************************************************

/**
 * @brief Timestamp formatter callback type
 *
 * User-defined function to format timestamps. The function receives the
 * raw microsecond timestamp and should write a formatted string to the buffer.
 *
 * @param timestamp_us Timestamp in microseconds (from log_get_timestamp_us)
 * @param buffer Output buffer for formatted timestamp string
 * @param buffer_size Size of output buffer
 * @return Number of characters written (excluding null terminator)
 */
typedef int (*log_timestamp_formatter_t)(uint64_t timestamp_us, char *buffer, size_t buffer_size);

/**
 * @brief Built-in timestamp format types
 *
 * @note LOG_TS_BACKEND_DEFAULT (0) is used in per-backend config to inherit
 *       the global timestamp setting. Zero-initialized structs will use global.
 * @note The three date/time formats need a wall clock (POSIX and ESP32 ports).
 *       Without one they print the LOG_TS_ELAPSED_SEC form instead.
 */
typedef enum {
    LOG_TS_BACKEND_DEFAULT = 0, // Use global setting (for per-backend config)
    LOG_TS_NONE,                // No timestamp
    LOG_TS_ELAPSED_US,          // Elapsed microseconds: "123456789"
    LOG_TS_ELAPSED_SEC,         // Elapsed seconds.micros: "1234.567890"
    LOG_TS_ELAPSED_MS,          // Elapsed milliseconds: "1234567"
    LOG_TS_DATETIME,            // Local date/time: "2026-09-26 13:56:44.149"
    LOG_TS_DATETIME_SHORT,      // Local DD/MM/YY-HH:MM:SS.mmm: "26/09/26-13:56:44.149" (default)
    LOG_TS_TIME_ONLY,           // Local time only: "13:56:44.149"
    LOG_TS_CUSTOM               // User-provided formatter callback
} log_timestamp_type_t;

/**
 * @brief Per-backend file/line display mode
 *
 * @note LOG_FILE_LINE_DEFAULT (0) follows the compile-time LOGGING_ENABLE_FILE_LINE
 *       setting. Zero-initialized structs will use global setting.
 */
typedef enum {
    LOG_FILE_LINE_DEFAULT = 0,  // Use compile-time LOGGING_ENABLE_FILE_LINE setting
    LOG_FILE_LINE_OFF,          // Never show file:line for this backend
    LOG_FILE_LINE_ON            // Always show file:line for this backend
} log_file_line_mode_t;

// *************************************************************************
//  Backend Driver Architecture
// *************************************************************************

/* Forward declaration for driver callbacks */
struct log_backend_cfg;

/**
 * @brief Backend driver structure
 *
 * Defines the operations for a logging backend. Built-in drivers are provided
 * for console and file output. Custom drivers can be created for any output.
 */
typedef struct {
    const char *name;  /**< Driver name for debugging (e.g., "console", "file") */

    /**
     * @brief Initialize backend
     * @param cfg Backend configuration
     * @return Internal state pointer (stored in cfg->_internal), or NULL
     */
    void *(*init)(struct log_backend_cfg *cfg);

    /**
     * @brief Deinitialize backend
     * @param internal Internal state pointer from init
     */
    void (*deinit)(void *internal);

    /**
     * @brief Write log message
     * @param internal Internal state pointer
     * @param cfg Backend configuration
     * @param level Log level (0-7)
     * @param tag Module tag (may be NULL)
     * @param file Source file (may be NULL)
     * @param line Source line
     * @param timestamp_str Formatted timestamp
     * @param message Formatted message
     */
    void (*write)(void *internal, const struct log_backend_cfg *cfg,
                  unsigned int level, const char *tag,
                  const char *file, int line,
                  const char *timestamp_str, const char *message);
} log_backend_driver_t;

// *************************************************************************
//  Built-in Backend Drivers
// *************************************************************************

/** @brief Console backend driver (stdout) */
extern const log_backend_driver_t log_driver_console;

/** @brief File backend driver (append to file) */
extern const log_backend_driver_t log_driver_file;

/** @brief STM32 Serial backend driver (UART via HAL) */
extern const log_backend_driver_t log_driver_serial_stm32;

/** @brief ESP32 Serial backend driver (UART via ESP-IDF) */
extern const log_backend_driver_t log_driver_serial_esp32;

// *************************************************************************
//  Platform Initialization (optional, for embedded platforms)
// *************************************************************************

/**
 * @brief Initialize platform-specific logging resources
 *
 * Call this early in application startup, before other threads or tasks
 * log, on platforms that require mutex initialization (STM32 with FreeRTOS,
 * ESP32). Until it runs those platforms log without a lock.
 * Safe to call multiple times.
 */
void log_platform_init(void);

/**
 * @brief Release platform-specific logging resources
 *
 * Deletes the mutex on ESP32. No-op on the other shipped ports.
 * Call only after log_deinit() and after every other thread stopped logging.
 */
void log_platform_deinit(void);

// *************************************************************************
//  Backend Configuration Structure
// *************************************************************************

/**
 * @brief Backend configuration
 *
 * Each backend receives log messages independently and can filter
 * based on its own level setting. Backends are driven by driver callbacks.
 *
 * @note If driver is NULL, the type field is used to select a built-in driver.
 * @note Zero-initialized optional fields use global defaults.
 */
typedef struct log_backend_cfg {
    // Backend identification and driver
    log_output_type_t type;              /**< Backend type (for readability/fallback) */
    const log_backend_driver_t *driver;  /**< Driver (NULL = use type for built-in) */

    // Runtime control
    int level;                   /**< Filter level (-1 to disable, 0-7 for levels). After log_init() change it only with log_backend_set_level() */
    bool enabled;                /**< Runtime switch. After log_init() prefer log_backend_enable() */
    void *config;                /**< Backend-specific config (e.g., file path) */

    // Per-backend format options (zero-init = use global defaults):
    log_timestamp_type_t timestamp_format;        /**< Timestamp format (0 = use global) */
    log_timestamp_formatter_t custom_ts_formatter; /**< Custom formatter for this backend */
    log_file_line_mode_t file_line_mode;          /**< File/line display mode (0 = use global) */

    // Backend behavior options:
    bool keep_open;              /**< For FILE backend: keep file open between writes */

    // Internal state - managed by framework, do not modify:
    void *_internal;             /**< Runtime state (e.g., open FILE* handle) */
} log_backend_cfg_t;

// *************************************************************************
//  Legacy Custom Backend Support (deprecated, use driver instead)
// *************************************************************************

/**
 * @brief Custom backend write callback (legacy)
 * @deprecated Use log_backend_driver_t instead
 */
typedef void (*log_custom_callback_t)(unsigned int level, const char *tag,
                                       const char *file, int line,
                                       const char *message);

// *************************************************************************
//  Core API Functions
// *************************************************************************

/**
 * @brief Initialize logging with backend array
 *
 * The array is stored by reference (not copied), so it must remain valid
 * until log_deinit() returns. Calls backend init functions for each backend.
 * A backend set installed by an earlier log_init() is deinitialized first.
 * Takes the logging lock. Must not be called from a driver or formatter.
 *
 * @param backends Array of backend configurations
 * @param count Number of backends in array
 */
void log_init(log_backend_cfg_t *backends, size_t count);

/**
 * @brief Report whether log_init() installed a backend set
 *
 * @return true between log_init() and log_deinit()
 */
bool log_is_initialized(void);

/**
 * @brief Deinitialize logging and release resources
 *
 * Calls backend deinit functions for each backend (e.g., closes open files),
 * then reverts to the built-in console backend. Takes the logging lock.
 * Stop other logging threads first: a call that is already past the lock
 * timeout may still be writing. Must not be called from a driver or formatter.
 */
void log_deinit(void);

/**
 * @brief Enable or disable a backend at runtime
 *
 * @param index Backend index (0-based)
 * @param enabled true to enable, false to disable
 */
void log_backend_enable(size_t index, bool enabled);

/**
 * @brief Set log level for a backend at runtime
 *
 * @param index Backend index (0-based)
 * @param level Log level (-1 to disable, 0-7 for levels)
 */
void log_backend_set_level(size_t index, int level);

/**
 * @brief Get backend enabled state
 *
 * @param index Backend index (0-based)
 * @return true if enabled, false if disabled or invalid index
 */
bool log_backend_is_enabled(size_t index);

/**
 * @brief Get backend level
 *
 * @param index Backend index (0-based)
 * @return Level, or LOGGING_LEVEL_NONE if invalid index
 */
int log_backend_get_level(size_t index);

/**
 * @brief Set per-backend timestamp format at runtime
 *
 * @param index Backend index (0-based)
 * @param type Timestamp format (LOG_TS_BACKEND_DEFAULT to use global)
 */
void log_backend_set_timestamp_format(size_t index, log_timestamp_type_t type);

/**
 * @brief Set per-backend custom timestamp formatter at runtime
 *
 * @param index Backend index (0-based)
 * @param formatter Custom formatter (NULL to clear)
 */
void log_backend_set_timestamp_formatter(size_t index, log_timestamp_formatter_t formatter);

/**
 * @brief Set per-backend file/line display mode at runtime
 *
 * @param index Backend index (0-based)
 * @param mode File/line display mode
 */
void log_backend_set_file_line_mode(size_t index, log_file_line_mode_t mode);

/**
 * @brief Get per-backend timestamp format
 *
 * @param index Backend index (0-based)
 * @return Timestamp format, or LOG_TS_BACKEND_DEFAULT if invalid index
 */
log_timestamp_type_t log_backend_get_timestamp_format(size_t index);

/**
 * @brief Get per-backend file/line mode
 *
 * @param index Backend index (0-based)
 * @return File/line mode, or LOG_FILE_LINE_DEFAULT if invalid index
 */
log_file_line_mode_t log_backend_get_file_line_mode(size_t index);

// *************************************************************************
//  Global Timestamp Configuration
// *************************************************************************

/**
 * @brief Set timestamp format using built-in formatter
 *
 * @param type Built-in timestamp format type
 */
void log_set_timestamp_format(log_timestamp_type_t type);

/**
 * @brief Set custom timestamp formatter callback
 *
 * A non-NULL formatter also sets the global format to LOG_TS_CUSTOM.
 * NULL clears the formatter and sets the global format to LOG_TS_NONE.
 *
 * @param formatter Custom formatter function (NULL to disable timestamps)
 */
void log_set_timestamp_formatter(log_timestamp_formatter_t formatter);

/**
 * @brief Get current timestamp type
 *
 * @return Current timestamp format type
 */
log_timestamp_type_t log_get_timestamp_format(void);

// *************************************************************************
//  Platform Hooks (Port Interface)
// *************************************************************************
//
// Each shipped port (src/platforms/linux.c, stm32.c, esp32.c) defines all of
// the hooks below, together with log_platform_init() and log_platform_deinit().
// Exactly one port is selected at compile time (see logging_platform.h).
//
// On targets without a shipped port, or when LOGGING_CUSTOM_PLATFORM is
// defined, the core supplies weak defaults: timestamp 0, no lock, and a
// single guard flag. Application code may override any subset of them.
// A port for a multi-threaded system must override the lock and the guard.

/**
 * @brief Get current timestamp in microseconds
 *
 * Must be monotonic. Generic default returns 0.
 *
 * @return Timestamp in microseconds
 */
uint64_t log_get_timestamp_us(void);

/**
 * @brief Acquire logging lock with timeout
 *
 * Generic default returns true (no locking).
 *
 * @param timeout_ms Timeout in milliseconds
 * @return true if lock acquired, false on timeout
 */
bool log_lock(unsigned int timeout_ms);

/**
 * @brief Release logging lock
 *
 * Called only after log_lock() returned true. Generic default does nothing.
 */
void log_unlock(void);

/**
 * @brief Mark the calling thread as inside the logging library
 *
 * Prevents recursion: a log call made from a driver, a formatter or a hook
 * on the same thread is discarded. Must be per thread (or per task).
 * Generic default uses one flag for the whole program.
 *
 * @return false if the calling thread is already inside, true otherwise
 */
bool log_guard_enter(void);

/**
 * @brief Leave the logging library (after log_guard_enter() returned true)
 */
void log_guard_exit(void);

// *************************************************************************
//  Internal Logging Function (Use Macros Instead)
// *************************************************************************

// printf format checking for log_write() on GCC-compatible compilers
#if defined(__GNUC__) || defined(__clang__)
#define LOG_PRINTF_FORMAT(fmt_index, args_index) __attribute__((format(printf, fmt_index, args_index)))
#else
#define LOG_PRINTF_FORMAT(fmt_index, args_index)
#endif

/**
 * @brief Core logging function (use macros instead of calling directly)
 *
 * @param level Log level (0-7)
 * @param tag Module/component tag (may be NULL)
 * @param file Source file name (may be NULL)
 * @param line Source line number (0 if file is NULL)
 * @param format printf-style format string (NULL prints "(null format)")
 * @param ... Variable arguments
 */
void log_write(unsigned int level, const char *tag, const char *file,
               int line, const char *format, ...) LOG_PRINTF_FORMAT(5, 6);

/**
 * @brief Message formatter callback for log_write_cb()
 *
 * Writes the message text into buffer (buffer_size bytes, terminator
 * included). The buffer is cleared before the call and terminated after it.
 *
 * @return Length of the message written (excluding terminator)
 */
typedef size_t (*log_message_formatter_t)(char *buffer, size_t buffer_size, void *context);

/**
 * @brief Log a message produced by a formatter callback
 *
 * Same filtering, locking and dispatch as log_write(). The formatter runs
 * under the logging lock, only when at least the fast filter accepts the
 * level, and writes straight into the message buffer.
 *
 * @param level Log level (0-7)
 * @param tag Module/component tag (may be NULL)
 * @param file Source file name (may be NULL)
 * @param line Source line number (0 if file is NULL)
 * @param formatter Message formatter (NULL gives an empty message)
 * @param context Passed to the formatter unchanged
 */
void log_write_cb(unsigned int level, const char *tag, const char *file,
                  int line, log_message_formatter_t formatter, void *context);

// *************************************************************************
//  Logging Macros (short names LOG_EMERG() .. LOG_DEBUG(): logging_short.h)
// *************************************************************************

#ifdef LOGGING_ENABLE_FILE_LINE
    #define LOGGING_WHERE __FILE__, __LINE__
#else
    #define LOGGING_WHERE NULL, 0
#endif

#ifndef LOGGING_DISABLE_LOGGING
    // A call above LOGGING_MAX_COMPILED_LEVEL is a constant false condition:
    // no code, no strings and no evaluated arguments remain, even at -O0,
    // but the compiler still checks the format and the argument types.
    #define LOGGING_CALL(level, tag, fmt, ...) \
        ((void)((int)(level) <= (LOGGING_MAX_COMPILED_LEVEL) && \
                (log_write(level, tag, LOGGING_WHERE, fmt, ##__VA_ARGS__), 1)))

    #define LOGGING_EMERG(tag, fmt, ...)   LOGGING_CALL(LOGGING_LEVEL_EMERG, tag, fmt, ##__VA_ARGS__)
    #define LOGGING_ALERT(tag, fmt, ...)   LOGGING_CALL(LOGGING_LEVEL_ALERT, tag, fmt, ##__VA_ARGS__)
    #define LOGGING_CRIT(tag, fmt, ...)    LOGGING_CALL(LOGGING_LEVEL_CRIT, tag, fmt, ##__VA_ARGS__)
    #define LOGGING_ERR(tag, fmt, ...)     LOGGING_CALL(LOGGING_LEVEL_ERR, tag, fmt, ##__VA_ARGS__)
    #define LOGGING_WARNING(tag, fmt, ...) LOGGING_CALL(LOGGING_LEVEL_WARNING, tag, fmt, ##__VA_ARGS__)
    #define LOGGING_NOTICE(tag, fmt, ...)  LOGGING_CALL(LOGGING_LEVEL_NOTICE, tag, fmt, ##__VA_ARGS__)
    #define LOGGING_INFO(tag, fmt, ...)    LOGGING_CALL(LOGGING_LEVEL_INFO, tag, fmt, ##__VA_ARGS__)
    #define LOGGING_DEBUG(tag, fmt, ...)   LOGGING_CALL(LOGGING_LEVEL_DEBUG, tag, fmt, ##__VA_ARGS__)
#else
    // Logging disabled - all macros become no-ops
    #define LOGGING_CALL(level, tag, fmt, ...) ((void)0)
    #define LOGGING_EMERG(tag, fmt, ...)   ((void)0)
    #define LOGGING_ALERT(tag, fmt, ...)   ((void)0)
    #define LOGGING_CRIT(tag, fmt, ...)    ((void)0)
    #define LOGGING_ERR(tag, fmt, ...)     ((void)0)
    #define LOGGING_WARNING(tag, fmt, ...) ((void)0)
    #define LOGGING_NOTICE(tag, fmt, ...)  ((void)0)
    #define LOGGING_INFO(tag, fmt, ...)    ((void)0)
    #define LOGGING_DEBUG(tag, fmt, ...)   ((void)0)
#endif

#ifdef __cplusplus
}
#endif

#endif /* __LOGGING_H */
