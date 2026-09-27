/**
  ******************************************************************************
  * @file           : logging_core.c
  * @brief          : Core implementation of multi-backend logging framework
  * @note           : Main logging API and message dispatch
  ******************************************************************************
  * @attention
  *
  * (C) 2025 Anatoli Arkhipenko
  * This code is provided as-is, without warranty of any kind.
  */

// Feature test macros must come before any includes (localtime_r)
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200112L
#endif

#include "logging.h"
#include "logging_internal.h"
#include <stdio.h>
#include <string.h>
#include <time.h>

#if LOGGING_HAVE_WALLCLOCK
#include <sys/time.h>
#endif

// *************************************************************************
//  Global State
// *************************************************************************

#ifndef LOGGING_DISABLE_LOGGING
// Default console backend used when log_init() not called
static log_backend_cfg_t g_default_backend = {
    .type = LOG_OUTPUT_CONSOLE,
    .driver = NULL,  // Will use type-based lookup
    .level = LOGGING_LEVEL_DEBUG,
    .enabled = true,
    .config = NULL,
};
#endif

log_state_t g_log_state = {
    .backends = NULL,
    .backend_count = 0,
    .initialized = false,
    .busy = false,
    .max_level = LOGGING_LEVEL_DEBUG,              // Level of the default backend
    .timestamp_type = LOG_TS_DATETIME_SHORT,   // Default: DD/MM/YY-HH:MM:SS.mmm
    .custom_formatter = NULL,
};

// *************************************************************************
//  Built-in Driver Lookup Table
// *************************************************************************

/**
 * @brief Lookup table for built-in drivers indexed by log_output_type_t
 *
 * Allows O(1) driver resolution without switch statements.
 * Entries for unimplemented types are NULL.
 */
static const log_backend_driver_t *g_builtin_drivers[] = {
    [LOG_OUTPUT_NONE]     = NULL,
    [LOG_OUTPUT_CONSOLE]  = &log_driver_console,
    [LOG_OUTPUT_FILE]     = &log_driver_file,
    [LOG_OUTPUT_SYSLOG]   = NULL,  // Not implemented
    [LOG_OUTPUT_JOURNALD] = NULL,  // Not implemented
    [LOG_OUTPUT_CUSTOM]   = NULL,  // Must use driver field
};

#define BUILTIN_DRIVER_COUNT (sizeof(g_builtin_drivers) / sizeof(g_builtin_drivers[0]))

/**
 * @brief Get driver for backend configuration
 */
const log_backend_driver_t *log_get_driver(const log_backend_cfg_t *cfg) {
    if (!cfg) {
        return NULL;
    }
    // Use explicit driver if set
    if (cfg->driver) {
        return cfg->driver;
    }
    // Fall back to type-based lookup
    if ((unsigned)cfg->type < BUILTIN_DRIVER_COUNT) {
        return g_builtin_drivers[cfg->type];
    }
    return NULL;
}

// *************************************************************************
//  Platform Hook Defaults (generic platform only)
// *************************************************************************
//
// A shipped port defines every hook as a strong symbol, so the core defines
// none for it. Without a shipped port the defaults below are weak and can be
// overridden one by one. On a compiler without weak symbols they are strong,
// and LOGGING_CUSTOM_PLATFORM removes them so the application supplies all.

#if defined(LOGGING_PLATFORM_GENERIC) && (LOG_HAVE_WEAK || !defined(LOGGING_CUSTOM_PLATFORM))

// One flag for the whole program: correct for a single thread plus interrupts
static volatile int log_default_guard = 0;

LOG_WEAK uint64_t log_get_timestamp_us(void) {
    return 0;
}

LOG_WEAK bool log_lock(unsigned int timeout_ms) {
    (void)timeout_ms;
    return true;
}

LOG_WEAK void log_unlock(void) {
    // No-op
}

LOG_WEAK void log_platform_init(void) {
    // No-op for platforms that don't need initialization
}

LOG_WEAK void log_platform_deinit(void) {
    // No-op
}

LOG_WEAK bool log_guard_enter(void) {
    if (log_default_guard) {
        return false;
    }
    log_default_guard = 1;
    return true;
}

LOG_WEAK void log_guard_exit(void) {
    log_default_guard = 0;
}

#endif // LOGGING_PLATFORM_GENERIC

// *************************************************************************
//  String Helpers (no printf: its stack frame is large on small targets)
// *************************************************************************

/**
 * @brief Append a string, clamping the length to buffer_size - 1
 *
 * @return New length
 */
static size_t log_put_str(char *buffer, size_t buffer_size, size_t len, const char *s) {
    while (*s && len + 1 < buffer_size) {
        buffer[len++] = *s++;
    }
    buffer[len] = '\0';
    return len;
}

/**
 * @brief Append a character repeated count times
 */
static size_t log_put_chars(char *buffer, size_t buffer_size, size_t len, char c, int count) {
    while (count-- > 0 && len + 1 < buffer_size) {
        buffer[len++] = c;
    }
    buffer[len] = '\0';
    return len;
}

/**
 * @brief Append an unsigned value in decimal with at least min_digits digits
 */
static size_t log_put_uint(char *buffer, size_t buffer_size, size_t len,
                           unsigned long long value, int min_digits) {
    char digits[20];
    int count = 0;
    do {
        digits[count++] = (char)('0' + (int)(value % 10u));
        value /= 10u;
    } while (value != 0u && count < (int)sizeof(digits));
    len = log_put_chars(buffer, buffer_size, len, '0', min_digits - count);
    while (count > 0 && len + 1 < buffer_size) {
        buffer[len++] = digits[--count];
    }
    buffer[len] = '\0';
    return len;
}

// *************************************************************************
//  Built-in Timestamp Formatters
// *************************************************************************

/**
 * @brief Write an unsigned 64-bit value in decimal
 *
 * printf is not used because newlib-nano has no 64-bit conversions.
 */
static int log_u64_to_dec(uint64_t value, char *buffer, size_t buffer_size) {
    char digits[20];
    size_t count = 0;
    size_t len = 0;

    do {
        digits[count++] = (char)('0' + (int)(value % 10u));
        value /= 10u;
    } while (value != 0u && count < sizeof(digits));

    while (count > 0 && len + 1 < buffer_size) {
        buffer[len++] = digits[--count];
    }
    buffer[len] = '\0';
    return (int)len;
}

/**
 * @brief Format as elapsed microseconds: "123456789"
 */
static int ts_format_elapsed_us(const log_ts_ctx_t *ctx, char *buffer, size_t buffer_size) {
    return log_u64_to_dec(ctx->mono_us, buffer, buffer_size);
}

/**
 * @brief Format as elapsed seconds.micros: "1234.567890"
 */
static int ts_format_elapsed_sec(const log_ts_ctx_t *ctx, char *buffer, size_t buffer_size) {
    size_t len = (size_t)log_u64_to_dec(ctx->mono_us / 1000000u, buffer, buffer_size);
    len = log_put_str(buffer, buffer_size, len, ".");
    len = log_put_uint(buffer, buffer_size, len, ctx->mono_us % 1000000u, 6);
    return (int)len;
}

/**
 * @brief Format as elapsed milliseconds: "1234567"
 */
static int ts_format_elapsed_ms(const log_ts_ctx_t *ctx, char *buffer, size_t buffer_size) {
    return log_u64_to_dec(ctx->mono_us / 1000u, buffer, buffer_size);
}

/**
 * @brief Read the local wall clock once per log call
 *
 * @return true if ctx->wall_tm and ctx->wall_ms are valid
 */
static bool log_read_wall_clock(log_ts_ctx_t *ctx) {
    if (!ctx->wall_read) {
        ctx->wall_read = true;
#if LOGGING_HAVE_WALLCLOCK
        struct timeval tv;
        if (gettimeofday(&tv, NULL) == 0) {
            time_t seconds = (time_t)tv.tv_sec;
            if (localtime_r(&seconds, &ctx->wall_tm) != NULL) {
                ctx->wall_ms = (int)(tv.tv_usec / 1000);
                ctx->wall_ok = true;
            }
        }
#endif
    }
    return ctx->wall_ok;
}

/**
 * @brief Format the wall clock with strftime() plus ".mmm"
 *
 * Falls back to elapsed seconds when no wall clock is available.
 */
static int ts_format_wall_clock(log_ts_ctx_t *ctx, const char *format,
                                char *buffer, size_t buffer_size) {
    if (!log_read_wall_clock(ctx)) {
        return ts_format_elapsed_sec(ctx, buffer, buffer_size);
    }

    size_t len = strftime(buffer, buffer_size, format, &ctx->wall_tm);
    if (len == 0) {
        buffer[0] = '\0';  // Contents are undefined when strftime fails
        return 0;
    }
    if (len + 4 < buffer_size) {
        len = log_put_str(buffer, buffer_size, len, ".");
        len = log_put_uint(buffer, buffer_size, len, (unsigned long long)ctx->wall_ms, 3);
    }
    return (int)len;
}

// *************************************************************************
//  Timestamp Formatting API
// *************************************************************************

/**
 * @brief Core timestamp formatting with explicit type and formatter
 */
int log_format_timestamp_ex(log_timestamp_type_t type,
                            log_timestamp_formatter_t custom_formatter,
                            log_ts_ctx_t *ctx,
                            char *buffer, size_t buffer_size) {
    if (!buffer || buffer_size == 0) {
        return 0;
    }
    buffer[0] = '\0';
    if (!ctx) {
        return 0;
    }

    int len = 0;
    switch (type) {
        case LOG_TS_NONE:
            break;

        case LOG_TS_ELAPSED_US:
            len = ts_format_elapsed_us(ctx, buffer, buffer_size);
            break;

        case LOG_TS_ELAPSED_SEC:
            len = ts_format_elapsed_sec(ctx, buffer, buffer_size);
            break;

        case LOG_TS_ELAPSED_MS:
            len = ts_format_elapsed_ms(ctx, buffer, buffer_size);
            break;

        case LOG_TS_DATETIME:
            len = ts_format_wall_clock(ctx, "%Y-%m-%d %H:%M:%S", buffer, buffer_size);
            break;

        case LOG_TS_TIME_ONLY:
            len = ts_format_wall_clock(ctx, "%H:%M:%S", buffer, buffer_size);
            break;

        case LOG_TS_CUSTOM:
            if (custom_formatter) {
                len = custom_formatter(ctx->mono_us, buffer, buffer_size);
                buffer[buffer_size - 1] = '\0';  // Guard against a missing terminator
                if (len < 0) {
                    len = 0;
                }
            }
            break;

        case LOG_TS_BACKEND_DEFAULT:  // Only reached if the global format is 0
        case LOG_TS_DATETIME_SHORT:
        default:
            len = ts_format_wall_clock(ctx, "%d/%m/%y-%H:%M:%S", buffer, buffer_size);
            break;
    }
    return len;
}

/**
 * @brief Determine if file/line should be shown for given mode
 */
bool log_should_show_file_line(log_file_line_mode_t mode) {
    switch (mode) {
        case LOG_FILE_LINE_OFF:
            return false;
        case LOG_FILE_LINE_ON:
            return true;
        case LOG_FILE_LINE_DEFAULT:
        default:
#ifdef LOGGING_ENABLE_FILE_LINE
            return true;
#else
            return false;
#endif
    }
}

// *************************************************************************
//  Output Line Formatting
// *************************************************************************

size_t log_format_prefix(char *buffer, size_t buffer_size,
                         unsigned int level, const char *tag,
                         const char *file, int line,
                         const char *timestamp_str) {
    if (!buffer || buffer_size == 0) {
        return 0;
    }
    buffer[0] = '\0';

    size_t len = 0;
    if (timestamp_str && timestamp_str[0] != '\0') {
        len = log_put_str(buffer, buffer_size, len, "[");
        len = log_put_str(buffer, buffer_size, len, timestamp_str);
        len = log_put_str(buffer, buffer_size, len, "] ");
    }

    // Level name left-justified in 6 columns, then one space
    const char *level_str = log_get_level_string(level);
    size_t start = len;
    len = log_put_str(buffer, buffer_size, len, level_str);
    len = log_put_chars(buffer, buffer_size, len, ' ', 6 - (int)(len - start));
    len = log_put_str(buffer, buffer_size, len, " ");

    if (tag) {
        len = log_put_str(buffer, buffer_size, len, "[");
        len = log_put_str(buffer, buffer_size, len, tag);
        len = log_put_str(buffer, buffer_size, len, "] ");
    }
    if (file && line > 0) {
        len = log_put_str(buffer, buffer_size, len, "[");
        len = log_put_str(buffer, buffer_size, len, log_extract_filename(file));
        len = log_put_str(buffer, buffer_size, len, ":");
        len = log_put_uint(buffer, buffer_size, len, (unsigned long long)line, 1);
        len = log_put_str(buffer, buffer_size, len, "] ");
    }
    return len;
}

size_t log_format_line(char *buffer, size_t buffer_size,
                       unsigned int level, const char *tag,
                       const char *file, int line,
                       const char *timestamp_str, const char *message,
                       const char *eol) {
    if (!buffer || buffer_size == 0) {
        return 0;
    }
    buffer[0] = '\0';

    if (!eol) {
        eol = "";
    }
    size_t eol_len = strlen(eol);
    if (buffer_size <= eol_len) {
        return 0;
    }

    // Reserve room for eol so a truncated line keeps its terminator
    size_t room = buffer_size - eol_len;
    size_t len = log_format_prefix(buffer, room, level, tag, file, line, timestamp_str);
    len = log_put_str(buffer, room, len, message ? message : "");
    memcpy(buffer + len, eol, eol_len + 1);
    return len + eol_len;
}

// *************************************************************************
//  Internal Helpers
// *************************************************************************

#ifndef LOGGING_DISABLE_LOGGING

/**
 * @brief True when a backend with the given threshold accepts the level
 */
static bool log_level_accepted(unsigned int level, int threshold) {
    return threshold >= 0 && level <= (unsigned int)threshold;
}

/**
 * @brief Recompute the fast-filter level (call with the lock held)
 *
 * Disabled backends count as well, so a direct write to 'enabled' is still
 * seen. A level change after log_init() must use log_backend_set_level().
 */
static void log_update_max_level(void) {
    int max_level = LOGGING_LEVEL_NONE;
    if (g_log_state.initialized && g_log_state.backends) {
        for (size_t i = 0; i < g_log_state.backend_count; i++) {
            if (g_log_state.backends[i].level > max_level) {
                max_level = g_log_state.backends[i].level;
            }
        }
    } else {
        max_level = g_default_backend.level;
    }
    g_log_state.max_level = max_level;
}

/**
 * @brief Enter a control call (setter): guard and lock when possible
 *
 * A control call made from inside a driver or formatter on the same thread
 * runs without taking the lock again.
 */
typedef struct {
    bool entered;
    bool locked;
} log_ctl_t;

static log_ctl_t log_ctl_begin(void) {
    log_ctl_t ctl;
    ctl.entered = log_guard_enter();
    ctl.locked = ctl.entered && log_lock(LOGGING_LOCK_TIMEOUT_MS);
    return ctl;
}

static void log_ctl_end(log_ctl_t ctl) {
    if (ctl.locked) {
        log_unlock();
    }
    if (ctl.entered) {
        log_guard_exit();
    }
}

/**
 * @brief Enter log_init()/log_deinit(): wait for the lock, mark state busy
 *
 * @return false when called from inside a log call on the same thread
 */
static bool log_lifecycle_begin(void) {
    if (!log_guard_enter()) {
        return false;
    }
    while (!log_lock(LOGGING_LOCK_TIMEOUT_MS)) {
        // Wait for the current holder: a log call with bounded driver I/O
    }
    g_log_state.busy = true;
    return true;
}

static void log_lifecycle_end(void) {
    g_log_state.busy = false;
    log_unlock();
    log_guard_exit();
}

/**
 * @brief Deinitialize the installed backend set (call inside a lifecycle section)
 */
static void log_release_backends(void) {
    if (!g_log_state.initialized || !g_log_state.backends) {
        return;
    }

    log_backend_cfg_t *backends = g_log_state.backends;
    size_t count = g_log_state.backend_count;

    g_log_state.initialized = false;
    g_log_state.backends = NULL;
    g_log_state.backend_count = 0;

    for (size_t i = 0; i < count; i++) {
        log_backend_cfg_t *be = &backends[i];
        const log_backend_driver_t *drv = log_get_driver(be);
        if (drv && drv->deinit) {
            drv->deinit(be->_internal);
        }
        be->_internal = NULL;
    }
}

#endif // LOGGING_DISABLE_LOGGING

// *************************************************************************
//  Global Timestamp Configuration
// *************************************************************************

void log_set_timestamp_format(log_timestamp_type_t type) {
#ifndef LOGGING_DISABLE_LOGGING
    log_ctl_t ctl = log_ctl_begin();
    g_log_state.timestamp_type = type;
    if (type != LOG_TS_CUSTOM) {
        g_log_state.custom_formatter = NULL;
    }
    log_ctl_end(ctl);
#else
    (void)type;
#endif
}

void log_set_timestamp_formatter(log_timestamp_formatter_t formatter) {
#ifndef LOGGING_DISABLE_LOGGING
    log_ctl_t ctl = log_ctl_begin();
    g_log_state.custom_formatter = formatter;
    g_log_state.timestamp_type = formatter ? LOG_TS_CUSTOM : LOG_TS_NONE;
    log_ctl_end(ctl);
#else
    (void)formatter;
#endif
}

log_timestamp_type_t log_get_timestamp_format(void) {
#ifndef LOGGING_DISABLE_LOGGING
    return g_log_state.timestamp_type;
#else
    return LOG_TS_NONE;
#endif
}

// *************************************************************************
//  Core API Functions
// *************************************************************************

void log_init(log_backend_cfg_t *backends, size_t count) {
#ifndef LOGGING_DISABLE_LOGGING
    if (!backends || count == 0) {
        return;
    }
    if (!log_lifecycle_begin()) {
        return;
    }

    // Release a previously installed set (closes its files)
    log_release_backends();

    // Initialize each backend using its driver
    for (size_t i = 0; i < count; i++) {
        log_backend_cfg_t *be = &backends[i];
        be->_internal = NULL;  // Clear internal state

        const log_backend_driver_t *drv = log_get_driver(be);
        if (drv && drv->init) {
            be->_internal = drv->init(be);
        }
    }

    // Publish the new set only after every driver is initialized
    g_log_state.backends = backends;
    g_log_state.backend_count = count;
    g_log_state.initialized = true;
    log_update_max_level();

    log_lifecycle_end();
#else
    (void)backends;
    (void)count;
#endif
}

void log_deinit(void) {
#ifndef LOGGING_DISABLE_LOGGING
    if (!log_lifecycle_begin()) {
        return;
    }
    log_release_backends();
    log_update_max_level();
    log_lifecycle_end();
#endif
}

bool log_is_initialized(void) {
#ifndef LOGGING_DISABLE_LOGGING
    return g_log_state.initialized;
#else
    return false;
#endif
}

void log_backend_enable(size_t index, bool enabled) {
#ifndef LOGGING_DISABLE_LOGGING
    log_ctl_t ctl = log_ctl_begin();
    if (index < g_log_state.backend_count) {
        g_log_state.backends[index].enabled = enabled;
    }
    log_ctl_end(ctl);
#else
    (void)index;
    (void)enabled;
#endif
}

void log_backend_set_level(size_t index, int level) {
#ifndef LOGGING_DISABLE_LOGGING
    log_ctl_t ctl = log_ctl_begin();
    if (index < g_log_state.backend_count) {
        g_log_state.backends[index].level = level;
        log_update_max_level();
    }
    log_ctl_end(ctl);
#else
    (void)index;
    (void)level;
#endif
}

bool log_backend_is_enabled(size_t index) {
#ifndef LOGGING_DISABLE_LOGGING
    if (index < g_log_state.backend_count) {
        return g_log_state.backends[index].enabled;
    }
#else
    (void)index;
#endif
    return false;
}

int log_backend_get_level(size_t index) {
#ifndef LOGGING_DISABLE_LOGGING
    if (index < g_log_state.backend_count) {
        return g_log_state.backends[index].level;
    }
#else
    (void)index;
#endif
    return LOGGING_LEVEL_NONE;
}

void log_backend_set_timestamp_format(size_t index, log_timestamp_type_t type) {
#ifndef LOGGING_DISABLE_LOGGING
    log_ctl_t ctl = log_ctl_begin();
    if (index < g_log_state.backend_count) {
        g_log_state.backends[index].timestamp_format = type;
        if (type != LOG_TS_CUSTOM) {
            g_log_state.backends[index].custom_ts_formatter = NULL;
        }
    }
    log_ctl_end(ctl);
#else
    (void)index;
    (void)type;
#endif
}

void log_backend_set_timestamp_formatter(size_t index, log_timestamp_formatter_t formatter) {
#ifndef LOGGING_DISABLE_LOGGING
    log_ctl_t ctl = log_ctl_begin();
    if (index < g_log_state.backend_count) {
        g_log_state.backends[index].custom_ts_formatter = formatter;
        if (formatter) {
            g_log_state.backends[index].timestamp_format = LOG_TS_CUSTOM;
        }
    }
    log_ctl_end(ctl);
#else
    (void)index;
    (void)formatter;
#endif
}

void log_backend_set_file_line_mode(size_t index, log_file_line_mode_t mode) {
#ifndef LOGGING_DISABLE_LOGGING
    log_ctl_t ctl = log_ctl_begin();
    if (index < g_log_state.backend_count) {
        g_log_state.backends[index].file_line_mode = mode;
    }
    log_ctl_end(ctl);
#else
    (void)index;
    (void)mode;
#endif
}

log_timestamp_type_t log_backend_get_timestamp_format(size_t index) {
#ifndef LOGGING_DISABLE_LOGGING
    if (index < g_log_state.backend_count) {
        return g_log_state.backends[index].timestamp_format;
    }
#else
    (void)index;
#endif
    return LOG_TS_BACKEND_DEFAULT;
}

log_file_line_mode_t log_backend_get_file_line_mode(size_t index) {
#ifndef LOGGING_DISABLE_LOGGING
    if (index < g_log_state.backend_count) {
        return g_log_state.backends[index].file_line_mode;
    }
#else
    (void)index;
#endif
    return LOG_FILE_LINE_DEFAULT;
}

// *************************************************************************
//  Core Logging Function
// *************************************************************************

#ifndef LOGGING_DISABLE_LOGGING

/**
 * @brief Buffers of one log call
 *
 * One static instance serves every call made under the logging lock, so the
 * message buffer does not sit on the calling task's stack.
 */
typedef struct {
    log_ts_ctx_t ts_ctx;
    char timestamp[LOGGING_TIMESTAMP_BUFFER_SIZE];
    char message[LOG_MESSAGE_BUFFER_SIZE];
} log_scratch_t;

static log_scratch_t g_log_scratch;

/**
 * @brief Format the message once and hand it to every accepting backend
 */
static void log_emit(unsigned int level, const char *tag, const char *file, int line,
                     log_message_formatter_t formatter, void *context,
                     char *message, size_t message_size,
                     char *timestamp, size_t timestamp_size, log_ts_ctx_t *ts_ctx) {
    // Read the backend set (default console if not initialized)
    const log_backend_cfg_t *backends = g_log_state.backends;
    size_t backend_count = g_log_state.backend_count;
    if (!g_log_state.initialized || !backends) {
        backends = &g_default_backend;
        backend_count = 1;
    }

    // Read the clock once (formatting is done per backend)
    memset(ts_ctx, 0, sizeof(*ts_ctx));
    ts_ctx->mono_us = log_get_timestamp_us();

    // Format message once
    message[0] = '\0';
    if (formatter) {
        formatter(message, message_size, context);
        message[message_size - 1] = '\0';
    }

    // Dispatch to all enabled backends
    for (size_t i = 0; i < backend_count; i++) {
        const log_backend_cfg_t *be = &backends[i];

        // Check if backend accepts this message
        if (!be->enabled || !log_level_accepted(level, be->level)) {
            continue;
        }

        // Determine timestamp format for this backend
        log_timestamp_type_t ts_type = be->timestamp_format;
        log_timestamp_formatter_t ts_formatter = be->custom_ts_formatter;
        if (ts_type == LOG_TS_BACKEND_DEFAULT) {
            // Use global settings
            ts_type = g_log_state.timestamp_type;
            ts_formatter = g_log_state.custom_formatter;
        }

        // Format timestamp for this backend
        log_format_timestamp_ex(ts_type, ts_formatter, ts_ctx, timestamp, timestamp_size);

        // Determine file/line for this backend
        const char *be_file = NULL;
        int be_line = 0;
        if (log_should_show_file_line(be->file_line_mode) && file) {
            be_file = file;
            be_line = line;
        }

        // Dispatch via driver callbacks
        const log_backend_driver_t *drv = log_get_driver(be);
        if (drv && drv->write) {
            drv->write(be->_internal, be, level, tag, be_file, be_line,
                       timestamp, message);
        } else if (be->type == LOG_OUTPUT_CUSTOM && be->config) {
            // Legacy custom callback support (deprecated)
            log_custom_callback_t callback = (log_custom_callback_t)be->config;
            callback(level, tag, be_file, be_line, message);
        }
    }
}

/**
 * @brief Emit without the lock (lock timeout): small buffers on this stack
 *
 * Kept out of line so that the common, locked path does not carry these
 * buffers in its stack frame.
 */
static LOG_NOINLINE void log_emit_unlocked(unsigned int level, const char *tag,
                                           const char *file, int line,
                                           log_message_formatter_t formatter,
                                           void *context) {
    log_ts_ctx_t ts_ctx;
    char timestamp[LOGGING_TIMESTAMP_BUFFER_SIZE];
    char message[LOGGING_UNLOCKED_BUFFER_SIZE];
    log_emit(level, tag, file, line, formatter, context, message, sizeof(message),
             timestamp, sizeof(timestamp), &ts_ctx);
}

/**
 * @brief Filter, lock and dispatch one message (shared by all entry points)
 */
static void log_dispatch(unsigned int level, const char *tag, const char *file,
                         int line, log_message_formatter_t formatter, void *context) {
    // Fast filter: one integer read, no lock, no access to the backend array
    if (!log_level_accepted(level, g_log_state.max_level)) {
        return;
    }

    // Recursion guard: silently ignore re-entrant calls on this thread
    if (!log_guard_enter()) {
        return;
    }

    // Try to acquire lock. On timeout continue unlocked, unless the backend
    // set is being replaced or released right now.
    if (log_lock(LOGGING_LOCK_TIMEOUT_MS)) {
        log_emit(level, tag, file, line, formatter, context,
                 g_log_scratch.message, sizeof(g_log_scratch.message),
                 g_log_scratch.timestamp, sizeof(g_log_scratch.timestamp),
                 &g_log_scratch.ts_ctx);
        log_unlock();
    } else if (!g_log_state.busy) {
        log_emit_unlocked(level, tag, file, line, formatter, context);
    }

    log_guard_exit();
}

/**
 * @brief printf-style formatter context used by log_write()
 */
typedef struct {
    const char *format;
    va_list *args;
} log_vformat_ctx_t;

static size_t log_vformat(char *buffer, size_t buffer_size, void *context) {
    log_vformat_ctx_t *ctx = (log_vformat_ctx_t *)context;
    int n;
    if (ctx->format) {
        n = vsnprintf(buffer, buffer_size, ctx->format, *ctx->args);
    } else {
        n = (int)log_put_str(buffer, buffer_size, 0, "(null format)");
    }
    if (n < 0) {
        buffer[0] = '\0';
        return 0;
    }
    return ((size_t)n < buffer_size) ? (size_t)n : buffer_size - 1;
}

#endif // LOGGING_DISABLE_LOGGING

void log_write(unsigned int level, const char *tag, const char *file,
               int line, const char *format, ...) {
#ifndef LOGGING_DISABLE_LOGGING
    va_list args;
    va_start(args, format);
    log_vformat_ctx_t ctx = { format, &args };
    log_dispatch(level, tag, file, line, log_vformat, &ctx);
    va_end(args);
#else
    (void)level;
    (void)tag;
    (void)file;
    (void)line;
    (void)format;
#endif
}

void log_write_cb(unsigned int level, const char *tag, const char *file,
                  int line, log_message_formatter_t formatter, void *context) {
#ifndef LOGGING_DISABLE_LOGGING
    log_dispatch(level, tag, file, line, formatter, context);
#else
    (void)level;
    (void)tag;
    (void)file;
    (void)line;
    (void)formatter;
    (void)context;
#endif
}
