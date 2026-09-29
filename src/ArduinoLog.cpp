/**
  ******************************************************************************
  * @file           : ArduinoLog.cpp
  * @brief          : ArduinoLog-compatible interface and Arduino Print driver
  * @note           : Compiled only in Arduino builds (ARDUINO defined)
  ******************************************************************************
  * @attention
  *
  * (C) 2025 Anatoli Arkhipenko
  * This code is provided as-is, without warranty of any kind.
  */

#if defined(ARDUINO)

#include "ArduinoLog.h"
#include "logging_internal.h"
#include <string.h>

// *************************************************************************
//  Arduino Print Backend Driver
// *************************************************************************

/**
 * @brief Library level to ArduinoLog level character
 */
static char log_arduino_level_char(unsigned int level) {
    if (level <= LOGGING_LEVEL_CRIT) {
        return 'F';
    }
    switch (level) {
        case LOGGING_LEVEL_ERR:     return 'E';
        case LOGGING_LEVEL_WARNING: return 'W';
        case LOGGING_LEVEL_NOTICE:  return 'N';
        case LOGGING_LEVEL_INFO:    return 'T';
        default:                return 'V';
    }
}

/**
 * @brief STANDARD layout: library prefix, message, CR LF
 *
 * Out of line, so the ArduinoLog layout does not carry the prefix buffer.
 */
static LOG_NOINLINE void log_arduino_print_standard(Print *out, unsigned int level, const char *tag,
                                                    const char *file, int line,
                                                    const char *timestamp_str, const char *message) {
    char prefix[LOG_PREFIX_BUFFER_SIZE];
    log_format_prefix(prefix, sizeof(prefix), level, tag, file, line, timestamp_str);
    out->print(prefix);
    out->print(message);
    out->print("\r\n");
}

/**
 * @brief Write one record to the Print object in cfg->config
 *
 * ARDUINOLOG layout: prefix(output), "N: ", message, "\n", suffix(output).
 * The suffix follows the line end, as it did in ArduinoLog for messages
 * ending with CR.
 */
static void log_arduino_print_write(void *internal, const log_backend_cfg_t *cfg,
                                    unsigned int level, const char *tag,
                                    const char *file, int line,
                                    const char *timestamp_str, const char *message) {
    (void)internal;

    const log_arduino_print_cfg_t *pc = cfg ? (const log_arduino_print_cfg_t *)cfg->config : NULL;
    if (!pc || !pc->output) {
        return;
    }
    Print *out = pc->output;

    if (pc->layout == LOG_ARDUINO_LAYOUT_STANDARD) {
        log_arduino_print_standard(out, level, tag, file, line, timestamp_str, message);
        return;
    }

    if (pc->prefix) {
        pc->prefix(out);
    }
    if (pc->show_level) {
        out->print(log_arduino_level_char(level));
        out->print(": ");
    }
    out->print(message);
    out->print("\n");
    if (pc->suffix) {
        pc->suffix(out);
    }
}

const log_backend_driver_t log_driver_arduino_print = {
    "arduino_print",
    NULL,
    NULL,
    log_arduino_print_write,
};

// *************************************************************************
//  Logging Class
// *************************************************************************

Logging::Logging() : _level(ARDUINO_LOG_LEVEL_SILENT), _enabled(true) {
    _cfg.output = NULL;
    _cfg.show_level = true;
    _cfg.prefix = NULL;
    _cfg.suffix = NULL;
    _cfg.layout = LOG_ARDUINO_LAYOUT_ARDUINOLOG;
    memset(&_backend, 0, sizeof(_backend));
}

void Logging::begin(int level, Print *output, bool showLevel) {
#ifndef LOGGING_DISABLE_LOGGING
    setLevel(level);
    _cfg.show_level = showLevel;
    _cfg.output = output;

    log_platform_init();
    if (!log_is_initialized()) {
        memset(&_backend, 0, sizeof(_backend));
        _backend.type = LOG_OUTPUT_CUSTOM;
        _backend.driver = &log_driver_arduino_print;
        _backend.level = LOGGING_LEVEL_DEBUG;          // filtering is done by setLevel()
        _backend.enabled = true;
        _backend.config = &_cfg;
        _backend.timestamp_format = LOG_TS_NONE;   // the prefix callback prints the time
        _backend.file_line_mode = LOG_FILE_LINE_OFF;
        log_init(&_backend, 1);
    }
#else
    (void)level;
    (void)output;
    (void)showLevel;
#endif
}

void Logging::write(int level, const char *msg, const unsigned char *kinds,
                    const log_value_t *values, size_t count) {
#ifndef LOGGING_DISABLE_LOGGING
    if (!_enabled || level <= ARDUINO_LOG_LEVEL_SILENT || level > _level) {
        return;
    }
    log_write_values(logging_compat::to_library_level(level), NULL, NULL, 0, msg,
                     kinds, values, count, LOG_FORMAT_ARDUINOLOG | LOG_FORMAT_STRIP_EOL);
#else
    (void)level;
    (void)msg;
    (void)kinds;
    (void)values;
    (void)count;
#endif
}

void Logging::setLevel(int level) {
    if (level < ARDUINO_LOG_LEVEL_SILENT) {
        level = ARDUINO_LOG_LEVEL_SILENT;
    }
    if (level > ARDUINO_LOG_LEVEL_VERBOSE) {
        level = ARDUINO_LOG_LEVEL_VERBOSE;
    }
    _level = level;
}

int Logging::getLevel() const {
    return _level;
}

void Logging::setShowLevel(bool showLevel) {
    _cfg.show_level = showLevel;
}

bool Logging::getShowLevel() const {
    return _cfg.show_level;
}

void Logging::setPrefix(printfunction f) {
    _cfg.prefix = f;
}

void Logging::setSuffix(printfunction f) {
    _cfg.suffix = f;
}

void Logging::setOutput(Print *output) {
    _cfg.output = output;
}

void Logging::enable() {
    _enabled = true;
}

void Logging::disable() {
    _enabled = false;
}

#ifndef DISABLE_STATIC_LOG
Logging Log;
#endif

#endif // ARDUINO
