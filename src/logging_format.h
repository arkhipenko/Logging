/**
  ******************************************************************************
  * @file           : logging_format.h
  * @brief          : Typed-argument printf formatter for logging framework
  * @note           : Used by the ArduinoLog interface (ArduinoLog.h). Each
  *                   argument carries its type, so a conversion never reads
  *                   the wrong type or a missing argument.
  ******************************************************************************
  * @attention
  *
  * (C) 2025 Anatoli Arkhipenko
  * This code is provided as-is, without warranty of any kind.
  */

#ifndef __LOGGING_FORMAT_H
#define __LOGGING_FORMAT_H

#include "logging.h"

#ifdef __cplusplus
extern "C" {
#endif

// *************************************************************************
//  Typed Arguments
// *************************************************************************
//
// An argument list is two parallel arrays: one kind byte per argument
// (usually a constant table shared by every call with the same argument
// types) and one 8-byte value slot per argument. Only the union member that
// matches the kind is written and read, so a 32-bit value costs one store.

typedef enum {
    LOG_VAL_I32 = 1,    // v.i32: signed integer of 32 bits or less
    LOG_VAL_U32,        // v.u32: unsigned integer of 32 bits or less
    LOG_VAL_I64,        // v.i64: signed 64-bit integer
    LOG_VAL_U64,        // v.u64: unsigned 64-bit integer
    LOG_VAL_BOOL,       // v.u32: 0 or 1
    LOG_VAL_DOUBLE,     // v.d
    LOG_VAL_STRING,     // v.s (may be NULL)
    LOG_VAL_POINTER,    // v.p
    LOG_VAL_IPV4        // v.u32: a | b << 8 | c << 16 | d << 24
} log_value_kind_t;

typedef union {
    int32_t i32;
    uint32_t u32;
    long long i64;
    unsigned long long u64;
    double d;
    const char *s;
    const void *p;
} log_value_t;

// *************************************************************************
//  Options
// *************************************************************************

// Bare %X, %F, %D and %l keep their ArduinoLog meaning ("0x" prefix,
// 2 decimals, long decimal). Without it they follow printf.
#define LOG_FORMAT_ARDUINOLOG   0x01u

// Remove one trailing "\n" or "\r\n" (log_write_values() only)
#define LOG_FORMAT_STRIP_EOL    0x02u

// *************************************************************************
//  Functions
// *************************************************************************

/**
 * @brief Format a message from a format string and typed arguments
 *
 * Grammar: %[flags][width][.precision][length]conversion, flags "-+ #0",
 * width and precision as digits or '*', length hh h l ll j z t L.
 *
 * Conversions: d i u o x X c s p f F e E g G a A % (printf), plus
 * b (binary), B ("0b" + binary), t ("T"/"F"), T ("true"/"false"),
 * S and P (string), I (IPv4 address), D (double).
 *
 * The value is taken from the argument's real type. An explicit hh or h
 * truncates as in printf; otherwise the wider of the argument and the
 * length modifier is used. A missing argument prints "<?>", an argument
 * that does not fit the conversion prints "<!>", surplus arguments are
 * ignored. %n is not supported ("<!>").
 *
 * Integers, characters, strings, booleans, binary, IPv4 and pointers are
 * formatted by the library. Only float conversions in printf syntax use
 * snprintf(); bare %F/%D with LOG_FORMAT_ARDUINOLOG do not.
 *
 * @param buffer Output buffer (always terminated when buffer_size > 0)
 * @param buffer_size Size of output buffer
 * @param format Format string (NULL prints "(null format)")
 * @param kinds One log_value_kind_t per argument
 * @param values One value slot per argument
 * @param count Number of arguments
 * @param options LOG_FORMAT_* flags
 * @return Length written (excluding terminator)
 */
size_t log_format_values(char *buffer, size_t buffer_size, const char *format,
                         const unsigned char *kinds, const log_value_t *values,
                         size_t count, unsigned int options);

/**
 * @brief Log a message built by log_format_values()
 *
 * Formatting happens under the logging lock, directly in the message
 * buffer, and only when the level passes the fast filter.
 */
void log_write_values(unsigned int level, const char *tag, const char *file, int line,
                      const char *format, const unsigned char *kinds,
                      const log_value_t *values, size_t count, unsigned int options);

#ifdef __cplusplus
}
#endif

#endif /* __LOGGING_FORMAT_H */
