/**
  ******************************************************************************
  * @file           : ArduinoLog.h
  * @brief          : ArduinoLog-compatible interface for the logging framework
  * @note           : Drop-in replacement for thijse/Arduino-Log 1.0.x: class
  *                   Logging, global object Log, the same methods and macros.
  *                   Format strings accept full printf specifications
  *                   (%04x, %-8s, %.3f, %lu ...) and the ArduinoLog ones
  *                   (%t %T %b %B %S %I %P %D, bare %X %F %l).
  *                   Arguments keep their C++ type, so a wrong or missing
  *                   argument prints a marker instead of reading garbage.
  *                   Requires C++11, <type_traits> and a C library whose
  *                   snprintf supports floats and 64-bit integers
  *                   (ESP32; not AVR).
  ******************************************************************************
  * @attention
  *
  * (C) 2025 Anatoli Arkhipenko
  * This code is provided as-is, without warranty of any kind.
  */

#ifndef __ARDUINOLOG_H
#define __ARDUINOLOG_H

#include <Arduino.h>

// IPAddress (%I). Not every core declares it in Arduino.h: n-able 0.3.1
// (nRF52) has IPAddress.h but includes it only from Client.h. The header is
// included when it exists; cores built on ArduinoCore-API keep it in
// api/IPAddress.h. Without either header, IPAddress arguments are left out.
// A compiler without __has_include relies on Arduino.h, as before.
#if defined(__has_include)
#if __has_include(<IPAddress.h>)
#include <IPAddress.h>
#define LOG_COMPAT_IPADDRESS 1
#elif __has_include(<api/IPAddress.h>)
#include <api/IPAddress.h>
#define LOG_COMPAT_IPADDRESS 1
#else
#define LOG_COMPAT_IPADDRESS 0
#endif
#else
#define LOG_COMPAT_IPADDRESS 1
#endif

// ArduinoLog switch: DISABLE_LOGGING turns logging off as before. Define it
// as a global build flag so the library sources see it too.
#if defined(DISABLE_LOGGING) && !defined(LOGGING_DISABLE_LOGGING)
#define LOGGING_DISABLE_LOGGING
#endif

// The ArduinoLog level names below reuse LOG_LEVEL_* with ArduinoLog values,
// so the syslog-style short names of logging_short.h cannot share a file
// with them. logging.h itself defines only LOGGING_* names.
#if defined(LOG_LEVEL_EMERG)
#error "ArduinoLog.h: logging_short.h is already included here. Its LOG_LEVEL_* names have other values than ArduinoLog's: use the LOGGING_* names of logging.h in this file."
#endif

#include "logging.h"
#include "logging_format.h"
#include <stddef.h>
#include <type_traits>

// *************************************************************************
//  ArduinoLog Constants
// *************************************************************************

typedef void (*printfunction)(Print *);

#define ARDUINO_LOG_LEVEL_SILENT  0
#define ARDUINO_LOG_LEVEL_FATAL   1
#define ARDUINO_LOG_LEVEL_ERROR   2
#define ARDUINO_LOG_LEVEL_WARNING 3
#define ARDUINO_LOG_LEVEL_NOTICE  4
#define ARDUINO_LOG_LEVEL_TRACE   5
#define ARDUINO_LOG_LEVEL_VERBOSE 6

// ArduinoLog level names (same values). LOGGING_ARDUINOLOG_PREFIXED_ONLY
// leaves them out, for example to avoid NimBLE's LOG_LEVEL_ERROR (3).
#ifndef LOGGING_ARDUINOLOG_PREFIXED_ONLY
#define LOG_LEVEL_SILENT  0
#define LOG_LEVEL_FATAL   1
#define LOG_LEVEL_ERROR   2
#define LOG_LEVEL_WARNING 3
#define LOG_LEVEL_NOTICE  4
#define LOG_LEVEL_TRACE   5
#define LOG_LEVEL_VERBOSE 6
#endif

#define CR "\n"

// *************************************************************************
//  Arduino Print Backend Driver
// *************************************************************************

/**
 * @brief Line layout of the Arduino Print driver
 */
typedef enum {
    LOG_ARDUINO_LAYOUT_ARDUINOLOG = 0,  // prefix(), "N: ", message, "\n", suffix() - as ArduinoLog
    LOG_ARDUINO_LAYOUT_STANDARD         // "[ts] LEVEL  [tag] [file:line] message\r\n"
} log_arduino_layout_t;

/**
 * @brief Configuration of the Arduino Print driver (cfg->config)
 */
typedef struct {
    Print *output;                  // destination (Serial, ...); NULL = no output
    bool show_level;                // ARDUINOLOG layout: print "F: ", "E: " ... "V: "
    printfunction prefix;           // ARDUINOLOG layout: called before each line
    printfunction suffix;           // ARDUINOLOG layout: called after each line end
    log_arduino_layout_t layout;
} log_arduino_print_cfg_t;

/** @brief Backend driver that writes to an Arduino Print object */
extern const log_backend_driver_t log_driver_arduino_print;

// *************************************************************************
//  Argument Conversion (internal)
// *************************************************************************
//
// Each argument type maps to a kind (compile time) and a store function
// (run time). The kinds of one argument list form a constant table shared by
// every call with the same argument types; the call site only fills one
// 8-byte value slot per argument. Scalars are stored from a copy, so volatile
// variables work; String and IPAddress are read by reference.

#if defined(__GNUC__) || defined(__clang__)
#define LOG_COMPAT_INLINE inline __attribute__((always_inline))
#else
#define LOG_COMPAT_INLINE inline
#endif

namespace logging_compat {

// Unsupported types are rejected at compile time
template <typename T, typename Enable = void>
struct arg_traits {
    static_assert(sizeof(T) == 0,
                  "ArduinoLog: unsupported argument type (use a number, bool, char*, String, IPAddress or pointer)");
    static const unsigned char kind = 0;
    static void store(log_value_t &, const T &) {}
};

// Integers of any size and signedness (char included, bool excluded)
template <typename T>
struct arg_traits<T, typename std::enable_if<std::is_integral<T>::value &&
                                             !std::is_same<T, bool>::value>::type> {
    static const unsigned char kind =
        (sizeof(T) > 4) ? (std::is_signed<T>::value ? LOG_VAL_I64 : LOG_VAL_U64)
                        : (std::is_signed<T>::value ? LOG_VAL_I32 : LOG_VAL_U32);
    static LOG_COMPAT_INLINE void store(log_value_t &v, T x) {
        if (sizeof(T) > 4) {
            if (std::is_signed<T>::value) { v.i64 = (long long)x; } else { v.u64 = (unsigned long long)x; }
        } else {
            if (std::is_signed<T>::value) { v.i32 = (int32_t)x; } else { v.u32 = (uint32_t)x; }
        }
    }
};

template <>
struct arg_traits<bool> {
    static const unsigned char kind = LOG_VAL_BOOL;
    static LOG_COMPAT_INLINE void store(log_value_t &v, bool x) { v.u32 = x ? 1u : 0u; }
};

template <typename T>
struct arg_traits<T, typename std::enable_if<std::is_floating_point<T>::value>::type> {
    static const unsigned char kind = LOG_VAL_DOUBLE;
    static LOG_COMPAT_INLINE void store(log_value_t &v, T x) { v.d = (double)x; }
};

// Enumerations: their underlying integer
template <typename T>
struct arg_traits<T, typename std::enable_if<std::is_enum<T>::value>::type> {
    typedef typename std::underlying_type<T>::type U;
    static const unsigned char kind = arg_traits<U>::kind;
    static LOG_COMPAT_INLINE void store(log_value_t &v, T x) { arg_traits<U>::store(v, static_cast<U>(x)); }
};

// Strings: character pointers, flash strings, String and classes derived from it
template <typename T>
struct arg_traits<T *, typename std::enable_if<std::is_same<typename std::remove_cv<T>::type, char>::value ||
                                               std::is_same<typename std::remove_cv<T>::type, signed char>::value ||
                                               std::is_same<typename std::remove_cv<T>::type, unsigned char>::value ||
                                               std::is_same<typename std::remove_cv<T>::type, __FlashStringHelper>::value>::type> {
    static const unsigned char kind = LOG_VAL_STRING;
    static LOG_COMPAT_INLINE void store(log_value_t &v, T *x) { v.s = reinterpret_cast<const char *>(x); }
};

template <typename T>
struct arg_traits<T, typename std::enable_if<std::is_base_of<String, T>::value>::type> {
    static const unsigned char kind = LOG_VAL_STRING;
    static LOG_COMPAT_INLINE void store(log_value_t &v, const T &x) { v.s = x.c_str(); }
};

#if LOG_COMPAT_IPADDRESS
// IPv4 address
template <typename T>
struct arg_traits<T, typename std::enable_if<std::is_base_of<IPAddress, T>::value>::type> {
    static const unsigned char kind = LOG_VAL_IPV4;
    static LOG_COMPAT_INLINE void store(log_value_t &v, const T &x) {
        v.u32 = (uint32_t)x[0] | ((uint32_t)x[1] << 8) | ((uint32_t)x[2] << 16) | ((uint32_t)x[3] << 24);
    }
};
#endif

// Other pointers, and nullptr
template <typename T>
struct arg_traits<T *, typename std::enable_if<!std::is_same<typename std::remove_cv<T>::type, char>::value &&
                                               !std::is_same<typename std::remove_cv<T>::type, signed char>::value &&
                                               !std::is_same<typename std::remove_cv<T>::type, unsigned char>::value &&
                                               !std::is_same<typename std::remove_cv<T>::type, __FlashStringHelper>::value>::type> {
    static const unsigned char kind = LOG_VAL_POINTER;
    static LOG_COMPAT_INLINE void store(log_value_t &v, T *x) { v.p = (const void *)x; }
};

template <>
struct arg_traits<std::nullptr_t> {
    static const unsigned char kind = LOG_VAL_POINTER;
    static LOG_COMPAT_INLINE void store(log_value_t &v, std::nullptr_t) { v.p = NULL; }
};

// Constant kind table, one per distinct argument type list
template <typename... Args>
struct kind_table {
    static const unsigned char value[sizeof...(Args)];
};
template <typename... Args>
const unsigned char kind_table<Args...>::value[sizeof...(Args)] = { arg_traits<Args>::kind... };

// Fill the value slots
LOG_COMPAT_INLINE void store_all(log_value_t *) {}

template <typename A0, typename... Args>
LOG_COMPAT_INLINE void store_all(log_value_t *v, const A0 &a0, const Args &... args) {
    typedef typename std::decay<const A0>::type D;  // arrays decay to const pointers
    arg_traits<D>::store(*v, a0);
    store_all(v + 1, args...);
}

// Format argument: C string, flash string or String
LOG_COMPAT_INLINE const char *format_ptr(const char *s) { return s; }
LOG_COMPAT_INLINE const char *format_ptr(const __FlashStringHelper *s) { return reinterpret_cast<const char *>(s); }
LOG_COMPAT_INLINE const char *format_ptr(const String &s) { return s.c_str(); }

/**
 * @brief ArduinoLog level (1-6) to library level
 */
inline unsigned int to_library_level(int level) {
    switch (level) {
        case ARDUINO_LOG_LEVEL_FATAL:   return LOGGING_LEVEL_CRIT;
        case ARDUINO_LOG_LEVEL_ERROR:   return LOGGING_LEVEL_ERR;
        case ARDUINO_LOG_LEVEL_WARNING: return LOGGING_LEVEL_WARNING;
        case ARDUINO_LOG_LEVEL_NOTICE:  return LOGGING_LEVEL_NOTICE;
        case ARDUINO_LOG_LEVEL_TRACE:   return LOGGING_LEVEL_INFO;
        default:                        return LOGGING_LEVEL_DEBUG;
    }
}

}  // namespace logging_compat

// *************************************************************************
//  Logging Class (ArduinoLog API)
// *************************************************************************

class Logging {
  public:
    Logging();

    /**
     * @brief Start logging to a Print object
     *
     * Creates the platform mutex and, unless log_init() was already called,
     * installs one backend that writes to output in the ArduinoLog layout.
     *
     * @param level Messages at this ArduinoLog level or more severe are logged
     * @param output Destination (for example &Serial)
     * @param showLevel Print "F: ", "E: " ... "V: " before each message
     */
    void begin(int level, Print *output, bool showLevel = true);

    void setLevel(int level);
    int getLevel() const;
    void setShowLevel(bool showLevel);
    bool getShowLevel() const;
    void setPrefix(printfunction f);
    void setSuffix(printfunction f);
    void setOutput(Print *output);

    // Arguments are taken by reference: no String copies at the call site.
    // The second template argument is the library level of to_library_level().
    template <class T, typename... Args> LOG_COMPAT_INLINE void fatal(const T &msg, const Args &... args) {
        call<ARDUINO_LOG_LEVEL_FATAL, LOGGING_LEVEL_CRIT>(msg, args...);
    }
    template <class T, typename... Args> LOG_COMPAT_INLINE void error(const T &msg, const Args &... args) {
        call<ARDUINO_LOG_LEVEL_ERROR, LOGGING_LEVEL_ERR>(msg, args...);
    }
    template <class T, typename... Args> LOG_COMPAT_INLINE void warning(const T &msg, const Args &... args) {
        call<ARDUINO_LOG_LEVEL_WARNING, LOGGING_LEVEL_WARNING>(msg, args...);
    }
    template <class T, typename... Args> LOG_COMPAT_INLINE void notice(const T &msg, const Args &... args) {
        call<ARDUINO_LOG_LEVEL_NOTICE, LOGGING_LEVEL_NOTICE>(msg, args...);
    }
    template <class T, typename... Args> LOG_COMPAT_INLINE void trace(const T &msg, const Args &... args) {
        call<ARDUINO_LOG_LEVEL_TRACE, LOGGING_LEVEL_INFO>(msg, args...);
    }
    template <class T, typename... Args> LOG_COMPAT_INLINE void verbose(const T &msg, const Args &... args) {
        call<ARDUINO_LOG_LEVEL_VERBOSE, LOGGING_LEVEL_DEBUG>(msg, args...);
    }

  private:
    // Levels above LOGGING_MAX_COMPILED_LEVEL select the empty overload, so
    // nothing of the call is instantiated or kept
    template <int Level, int LibraryLevel, class T, typename... Args>
    LOG_COMPAT_INLINE void call(const T &msg, const Args &... args) {
        call<Level>(std::integral_constant<bool, (LibraryLevel <= (LOGGING_MAX_COMPILED_LEVEL))>(), msg, args...);
    }

    template <int Level, class T, typename... Args>
    LOG_COMPAT_INLINE void call(std::true_type, const T &msg, const Args &... args) {
        emit(Level, logging_compat::format_ptr(msg), args...);
    }

    template <int Level, class T, typename... Args>
    LOG_COMPAT_INLINE void call(std::false_type, const T &, const Args &...) {}

#ifndef LOGGING_DISABLE_LOGGING
    LOG_COMPAT_INLINE void emit(int level, const char *msg) {
        write(level, msg, NULL, NULL, 0);
    }

    template <typename A0, typename... Args>
    LOG_COMPAT_INLINE void emit(int level, const char *msg, const A0 &a0, const Args &... args) {
        log_value_t values[1 + sizeof...(Args)];
        logging_compat::store_all(values, a0, args...);
        write(level, msg,
              logging_compat::kind_table<typename std::decay<const A0>::type,
                                         typename std::decay<const Args>::type...>::value,
              values, 1 + sizeof...(Args));
    }
#else
    // Logging disabled: unnamed parameters, so nothing is read (volatile included)
    template <typename... Args>
    LOG_COMPAT_INLINE void emit(int, const char *, const Args &...) {}
#endif

    // Level check, level mapping and hand-off to the core (out of line)
    void write(int level, const char *msg, const unsigned char *kinds,
               const log_value_t *values, size_t count);

    int _level;
    log_arduino_print_cfg_t _cfg;
    log_backend_cfg_t _backend;
};

#ifndef DISABLE_STATIC_LOG
extern Logging Log;
#endif

#endif /* __ARDUINOLOG_H */
