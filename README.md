# Multi-Backend Logging Framework

A flexible, thread-safe logging framework for C with support for multiple simultaneous output backends.

## Features

- **Driver-Based Architecture**: Extensible callback-based backends - add new backends without modifying core code
- **Multi-Backend Output**: Send logs to multiple destinations simultaneously (console, file, UART, custom)
- **Per-Backend Filtering**: Each backend can have its own log level threshold
- **Per-Backend Formatting**: Different timestamp formats and file/line display per backend
- **Runtime Configuration**: Enable/disable backends and change levels at runtime
- **Configurable Timestamps**: 7 built-in formats plus custom formatter callback
- **Default Console Output**: Works without initialization - logs to console by default
- **Thread-Safe**: Mutex-based locking with timeout (pthread on POSIX, FreeRTOS on STM32 and ESP32)
- **Recursion-Safe**: A log call made from inside a driver or formatter is dropped instead of deadlocking (per thread, or per task on FreeRTOS)
- **Syslog-Compatible Levels**: 8 standard levels (EMERG through DEBUG)
- **Fail-Safe**: Silent error handling - logging never crashes your application
- **ArduinoLog Drop-In**: `ArduinoLog.h` replaces thijse/Arduino-Log 1.0.x without call-site changes, with full printf formatting (`%04x`, `%-8s`, `%.3f`) and type-safe arguments

## Quick Start

### Minimal Example (No Initialization Required)

```c
#include "logging.h"

int main(void) {
    // Works immediately - outputs to console by default
    LOGGING_INFO(NULL, "Application started");
    LOGGING_ERR(NULL, "Something went wrong: %d", error_code);
    return 0;
}
```

### Multi-Backend Example

```c
#include "logging.h"

static log_backend_cfg_t g_log_backends[] = {
    {
        .type = LOG_OUTPUT_CONSOLE,
        .level = LOGGING_LEVEL_DEBUG,
        .enabled = true,
    },
    {
        .type = LOG_OUTPUT_FILE,
        .level = LOGGING_LEVEL_WARNING,
        .enabled = true,
        .config = "/var/log/myapp.log",
        .keep_open = true,
    },
};

int main(void) {
    log_init(g_log_backends, sizeof(g_log_backends) / sizeof(g_log_backends[0]));

    LOGGING_INFO(NULL, "Application started");      // Console only (INFO < WARNING)
    LOGGING_WARNING(NULL, "Low memory");            // Both console and file
    LOGGING_ERR(NULL, "Connection failed");         // Both console and file

    log_deinit();  // Clean up (closes files)
    return 0;
}
```

## Backend Configuration

Each backend is configured with a `log_backend_cfg_t` structure:

```c
typedef struct {
    log_output_type_t type;              // Backend type
    const log_backend_driver_t *driver;  // Custom driver (NULL = use built-in)
    int level;                           // Filter level (-1 to disable, 0-7)
    bool enabled;                        // Runtime enable/disable
    void *config;                        // Backend-specific configuration

    // Per-backend format options:
    log_timestamp_type_t timestamp_format;        // Timestamp format (0 = use global)
    log_timestamp_formatter_t custom_ts_formatter; // Custom formatter for this backend
    log_file_line_mode_t file_line_mode;          // File/line display mode

    // File backend options:
    bool keep_open;                      // Keep file open between writes
} log_backend_cfg_t;
```

After `log_init()`, change `level` only with `log_backend_set_level()`. The library keeps the highest accepted level to reject filtered messages without taking the lock, and only the setter updates it.

### Built-in Drivers

| Type | Driver | Config | Description |
|------|--------|--------|-------------|
| `LOG_OUTPUT_CONSOLE` | `log_driver_console` | `NULL` | Output to stdout |
| `LOG_OUTPUT_FILE` | `log_driver_file` | `const char *path` | Append to file |
| `LOG_OUTPUT_CUSTOM` | `log_driver_serial_stm32` | `UART_HandleTypeDef*` | STM32 UART output |
| `LOG_OUTPUT_CUSTOM` | `log_driver_serial_esp32` | `uart_port_t` | ESP32 UART output |

The serial drivers send CR LF and use a 256-byte line buffer. A longer line is truncated and keeps its CR LF.

### Platform Support

Each platform file provides the lock, the time source and the recursion guard. The library selects exactly one at compile time (see `src/logging_platform.h`):

| Platform | Selected when | Files | Features |
|----------|---------------|-------|----------|
| STM32 (HAL) | a family macro is defined (`STM32F4`, `STM32L4`, ...) | `platforms/stm32.c`, `backends/serial_stm32.c` | HAL_GetTick, FreeRTOS or bare-metal lock |
| ESP32 (ESP-IDF, Arduino-ESP32) | `ESP_PLATFORM` or `ESP32` | `platforms/esp32.c`, `backends/serial_esp32.c` | esp_timer, FreeRTOS mutex |
| Linux/Unix/macOS | `__linux__`, `__unix__` or `__APPLE__` | `platforms/linux.c` | pthread mutex, clock_gettime, wall clock |
| Anything else | none of the above | (core defaults) | timestamp 0, no lock |

- Compile all platform files. The ones that do not match compile to nothing.
- The STM32 family macro (for example `STM32F4`) must be visible to the library. If your build defines only the device macro (for example `STM32F407xx`), add the family macro (`-DSTM32F4`).
- STM32 uses the FreeRTOS lock when `USE_FREERTOS` or `LOGGING_STM32_FREERTOS` is defined, or when `FreeRTOS.h` is on the include path. Define `LOGGING_STM32_BARE_METAL` to force the bare-metal lock.
- Define `LOGGING_CUSTOM_PLATFORM` to disable the shipped platforms and provide your own hooks (see Platform Functions below).

**STM32 Example:**
```c
#include "logging.h"

extern UART_HandleTypeDef huart2;

static log_backend_cfg_t backends[] = {
    {
        .type = LOG_OUTPUT_CUSTOM,
        .driver = &log_driver_serial_stm32,
        .level = LOGGING_LEVEL_DEBUG,
        .enabled = true,
        .config = &huart2,
    },
};

int main(void) {
    HAL_Init();
    SystemClock_Config();
    MX_USART2_UART_Init();

    log_platform_init();  // Creates the mutex with FreeRTOS, before the scheduler starts
    log_init(backends, 1);

    LOGGING_INFO(NULL, "STM32 started");
    // ...
}
```

**ESP32 Example:**
```c
#include "logging.h"
#include "driver/uart.h"

static log_backend_cfg_t backends[] = {
    {
        .type = LOG_OUTPUT_CUSTOM,
        .driver = &log_driver_serial_esp32,
        .level = LOGGING_LEVEL_DEBUG,
        .enabled = true,
        .config = (void*)UART_NUM_0,  // Default USB Serial
    },
};

void app_main(void) {
    // The UART driver must be installed on the port (Arduino: Serial.begin() does this)
    uart_driver_install(UART_NUM_0, 1024, 0, 0, NULL, 0);

    log_platform_init();  // Initialize mutex
    log_init(backends, 1);

    LOGGING_INFO(NULL, "ESP32 started");
    // ...
}
```

### Custom Driver Example

Create custom backends by implementing the driver interface. `logging_internal.h` provides `log_format_prefix()` and `log_format_line()`, which build the same line layout as the built-in drivers:

```c
#include "logging.h"
#include "logging_internal.h"

// Driver callbacks
static void *uart_init(log_backend_cfg_t *cfg) {
    return uart_hw_init(cfg->config);  // Return internal state
}

static void uart_deinit(void *internal) {
    uart_hw_close(internal);
}

static void uart_write(void *internal, const log_backend_cfg_t *cfg,
                       unsigned int level, const char *tag,
                       const char *file, int line,
                       const char *timestamp_str, const char *message) {
    char buf[256];
    size_t len = log_format_line(buf, sizeof(buf), level, tag, file, line,
                                 timestamp_str, message, "\r\n");
    uart_hw_send(internal, buf, len);
}

// Driver structure
const log_backend_driver_t uart_driver = {
    .name = "uart",
    .init = uart_init,
    .deinit = uart_deinit,
    .write = uart_write,
};

// Usage
static log_backend_cfg_t g_backends[] = {
    { .type = LOG_OUTPUT_CONSOLE, .level = LOGGING_LEVEL_INFO, .enabled = true },
    { .type = LOG_OUTPUT_CUSTOM, .driver = &uart_driver,
      .level = LOGGING_LEVEL_DEBUG, .enabled = true, .config = &uart_cfg },
};
```

## ArduinoLog Compatibility

`src/ArduinoLog.h` replaces thijse/Arduino-Log 1.0.x without changes to call sites: class `Logging`, global `Log`, `begin`, `setLevel`, `getLevel`, `setShowLevel`, `getShowLevel`, `setPrefix`, `setSuffix`, `setOutput`, `fatal` through `verbose`, the level constants `LOG_LEVEL_SILENT` through `LOG_LEVEL_VERBOSE` (0-6) and their `ARDUINO_LOG_LEVEL_*` aliases, `CR`, `DISABLE_LOGGING` and `DISABLE_STATIC_LOG`. Remove the ArduinoLog library from the project. Arduino builds of this library compile the interface (`src/ArduinoLog.cpp`). It needs C++11 and `<type_traits>` (ESP32; not AVR). Floats in printf syntax (`%.3f`, `%e`) need float support in the C library's `snprintf`; everything else, 64-bit integers included, is formatted by the library.

```cpp
#include <ArduinoLog.h>

void printTimestamp(Print *out) { out->print(millis()); out->print(' '); }

void setup() {
    Serial.begin(115200);
    Log.begin(LOG_LEVEL_VERBOSE, &Serial);  // creates the mutex, installs the Serial backend
    Log.setPrefix(printTimestamp);
    Log.notice("value 0x%04x, name %S, ok %t" CR, 26, String("pump"), true);
    // 1234 N: value 0x001a, name pump, ok T
}
```

Format specifications:

| Specification | Output |
|---------------|--------|
| `%d %i %u %o %x %X %c %s %p %f %F %e %g %a %%` with flags, width, precision or length | printf |
| bare `%x` | printf (lowercase hex) |
| bare `%X` | `0x` + uppercase hex (ArduinoLog). `%04X` is printf. |
| bare `%F`, `%D` | 2 decimals (ArduinoLog). `%.3F` is printf. |
| `%l` not followed by a conversion letter | long decimal (ArduinoLog). `%lu`, `%ld`, `%lx` are printf. |
| `%t`, `%T` | `T`/`F`, `true`/`false` |
| `%b`, `%B` | binary, `0b` + binary |
| `%S`, `%P` | String, flash string (`%s` accepts both too) |
| `%I` | IPAddress (`ArduinoLog.h` includes `IPAddress.h` when the core has it; otherwise IPAddress arguments are left out) |

- Arguments are taken by reference (a `String` is not copied) and keep their C++ type. A 64-bit value prints in full, even with `%d`. A missing argument prints `<?>`, an argument of the wrong kind prints `<!>`. A class other than `String` or `IPAddress` (for example `std::string`) is a compile error.
- Levels: FATAL to CRIT, ERROR to ERR, WARNING to WARNING, NOTICE to NOTICE, TRACE to INFO, VERBOSE to DEBUG.
- Level names: `ArduinoLog.h` defines the ArduinoLog names `LOG_LEVEL_SILENT` through `LOG_LEVEL_VERBOSE` (0-6). `logging.h` and `ArduinoLog.h` can be included in any order. `logging_short.h` cannot share a file with `ArduinoLog.h` (same `LOG_LEVEL_*` spelling, other values): either order stops with a compile error. NimBLE also defines `LOG_LEVEL_ERROR` (3), exactly as with ArduinoLog 1.0.3; define `LOGGING_ARDUINOLOG_PREFIXED_ONLY` to keep only the `ARDUINO_LOG_LEVEL_*` names and avoid that overlap.
- Output layout (as ArduinoLog): prefix callback, `N: `, message, newline, suffix callback. One trailing CR in the message is removed, so every call is one line.
- `Log.begin()` installs one backend (`log_driver_arduino_print`) unless `log_init()` was called before. Then it only sets the level and the output settings. `log_driver_arduino_print` can also be used in your own backend array with a `log_arduino_print_cfg_t` (layout `LOG_ARDUINO_LAYOUT_ARDUINOLOG` or `LOG_ARDUINO_LAYOUT_STANDARD`).
- Code size, measured with the ESP32 compiler on 20 typical calls: about 45 bytes per call site at `-Og` and 43 at `-Os` (ArduinoLog: 54 and 49). The library itself adds about 8-10 KB of fixed code (ArduinoLog: about 1.5 KB).
- Stack, measured with the ESP32 compiler: a call needs about 640 bytes at its deepest point (formatting an integer) and 320 bytes in the Print driver, plus the Serial write chain. The message buffer is static, not on the task stack. Integers and strings are formatted without printf; only printf-style floats (`%.3f`, `%e`, `%g`) use `snprintf`, whose frame is about 800 bytes more.
- Differences from ArduinoLog: a message longer than `LOG_MESSAGE_BUFFER_SIZE` - 1 characters (default 1023) is truncated; raise the size for long payloads (it costs static RAM, not stack); `semLog` does not exist (the library mutex replaces it); a message without CR is still a complete line.

## Timestamp Formats

The framework supports multiple timestamp formats, configurable globally or per-backend:

| Format | Example | Description |
|--------|---------|-------------|
| `LOG_TS_DATETIME_SHORT` | `26/09/26-13:56:44.149` | Default format (DD/MM/YY-HH:MM:SS.mmm) |
| `LOG_TS_DATETIME` | `2026-09-26 13:56:44.149` | Full ISO-style datetime |
| `LOG_TS_TIME_ONLY` | `13:56:44.149` | Time without date |
| `LOG_TS_ELAPSED_SEC` | `1234.567890` | Seconds since start |
| `LOG_TS_ELAPSED_MS` | `1234567` | Milliseconds since start |
| `LOG_TS_ELAPSED_US` | `1234567890` | Microseconds since start |
| `LOG_TS_NONE` | *(empty)* | No timestamp |
| `LOG_TS_CUSTOM` | *(user defined)* | Custom formatter callback |

- The three date/time formats use the local wall clock (`gettimeofday` and `localtime_r`). It is available on the POSIX and ESP32 platforms. On ESP32 it shows whatever the system clock holds, so set it first (for example with SNTP).
- On a platform without a wall clock (STM32, other targets) the date/time formats print the `LOG_TS_ELAPSED_SEC` form instead. For an RTC timestamp use a custom formatter (see `examples/stm32/logging_stm32_date_time.c`).
- The elapsed formats print the full 64-bit value on every target.

### Per-Backend Timestamp Configuration

```c
static log_backend_cfg_t g_backends[] = {
    {
        .type = LOG_OUTPUT_CONSOLE,
        .level = LOGGING_LEVEL_DEBUG,
        .enabled = true,
        .timestamp_format = LOG_TS_TIME_ONLY,    // Short format for console
        .file_line_mode = LOG_FILE_LINE_OFF,
    },
    {
        .type = LOG_OUTPUT_FILE,
        .level = LOGGING_LEVEL_DEBUG,
        .enabled = true,
        .config = "/var/log/myapp.log",
        .timestamp_format = LOG_TS_DATETIME,     // Full format for file
        .file_line_mode = LOG_FILE_LINE_ON,
        .keep_open = true,
    },
};
```

### Custom Timestamp Formatter

```c
int my_formatter(uint64_t timestamp_us, char *buffer, size_t size) {
    return snprintf(buffer, size, "T+%lu", (unsigned long)(timestamp_us / 1000000));
}

// Set globally
log_set_timestamp_formatter(my_formatter);

// Or per-backend (before log_init(), or with log_backend_set_timestamp_formatter())
g_backends[0].timestamp_format = LOG_TS_CUSTOM;
g_backends[0].custom_ts_formatter = my_formatter;
```

The formatter must write a terminated string. `log_set_timestamp_formatter(NULL)` disables timestamps (global format `LOG_TS_NONE`).

## Log Levels

Syslog-compatible levels (lower number = higher severity). `logging.h` defines only prefixed names:

| Level | Value | Level name | Macro | Description |
|-------|-------|------------|-------|-------------|
| NONE | -1 | `LOGGING_LEVEL_NONE` | | Backend accepts nothing |
| EMERG | 0 | `LOGGING_LEVEL_EMERG` | `LOGGING_EMERG()` | System unusable |
| ALERT | 1 | `LOGGING_LEVEL_ALERT` | `LOGGING_ALERT()` | Immediate action required |
| CRIT | 2 | `LOGGING_LEVEL_CRIT` | `LOGGING_CRIT()` | Critical conditions |
| ERR | 3 | `LOGGING_LEVEL_ERR` | `LOGGING_ERR()` | Error conditions |
| WARNING | 4 | `LOGGING_LEVEL_WARNING` | `LOGGING_WARNING()` | Warning conditions |
| NOTICE | 5 | `LOGGING_LEVEL_NOTICE` | `LOGGING_NOTICE()` | Normal but significant |
| INFO | 6 | `LOGGING_LEVEL_INFO` | `LOGGING_INFO()` | Informational |
| DEBUG | 7 | `LOGGING_LEVEL_DEBUG` | `LOGGING_DEBUG()` | Debug messages |

`LOGGING_LEVEL_MAX` equals `LOGGING_LEVEL_DEBUG`.

### Compiled-in Levels (`LOGGING_MAX_COMPILED_LEVEL`)

A backend's level decides what is shown at run time. `LOGGING_MAX_COMPILED_LEVEL` decides what exists in the binary at all: calls above it compile to nothing, so no backend level can bring them back.

```bash
# Keep EMERG .. INFO, remove every LOGGING_DEBUG() call
gcc -DLOGGING_MAX_COMPILED_LEVEL=LOGGING_LEVEL_INFO ...
```

```ini
; PlatformIO
build_flags = -DLOGGING_MAX_COMPILED_LEVEL=LOGGING_LEVEL_INFO
```

- Value: a number (-1 to 7) or a `LOGGING_LEVEL_*` name. Default `LOGGING_LEVEL_DEBUG` (everything compiled). `LOGGING_LEVEL_NONE` removes every call. Do not use a short name such as `LOG_LEVEL_INFO`: NimBLE and ArduinoLog give it other values. A name that is not defined in a file is a compile error at its first logging call, not a silent 0.
- A removed call leaves no code, no format string, no tag and no `__FILE__` string in the object, also at `-O0`. Its arguments are not evaluated, so do not put side effects in them.
- The compiler still checks the format and the arguments of a removed call, and a variable used only there gives no unused warning. That is the difference from `LOGGING_DISABLE_LOGGING`, which ignores the arguments completely.
- The ArduinoLog methods follow the same limit through their library level (`verbose` is DEBUG, `trace` is INFO, see ArduinoLog Compatibility). A removed method call does nothing and reads no argument. Its string literals are dropped when optimizing (`-Os`, `-Og` and above; Arduino builds use `-Os`); at `-O0` they stay in the object.
- Only the files that log need the flag, the library sources do not. Pass it as a global build flag anyway, so every file uses the same limit.
- Calling `log_write()` directly is not affected.

### Short Names (`logging_short.h`)

Other libraries use the short spellings with other values. NimBLE (ESP-IDF `log_common.h`) defines `LOG_LEVEL_DEBUG` 0, `LOG_LEVEL_INFO` 1, `LOG_LEVEL_NONE` 5 and `LOG_LEVEL_MAX` 15. `<syslog.h>` and lwIP (`pppdebug.h`) define `LOG_ERR`, `LOG_INFO` and others as numbers. ArduinoLog uses `LOG_LEVEL_SILENT` .. `LOG_LEVEL_VERBOSE` (0-6). When two headers define the same macro, the last one wins and gcc only warns. So `logging.h` keeps out of that namespace.

For the short names, include `logging_short.h` instead of `logging.h`, in the files that want them:

```c
#include "logging_short.h"   // logging.h plus LOG_LEVEL_* and LOG_EMERG() .. LOG_DEBUG()

static log_backend_cfg_t backends[] = {
    { .type = LOG_OUTPUT_CONSOLE, .level = LOG_LEVEL_DEBUG, .enabled = true },
};
...
LOG_INFO("app", "started");
```

- A file that also sees NimBLE, `ArduinoLog.h` or `<syslog.h>` uses the `LOGGING_*` names instead.
- `logging_short.h` stops with `#error` when one of its names is already defined by a header included before it. A header included after it can still redefine them: gcc then warns `"LOG_LEVEL_DEBUG" redefined`, and that warning is the only sign.
- Your own prefix: copy `logging_short.h`, replace every `LOG_` with your prefix (for example `APP_`, giving `APP_LEVEL_DEBUG` and `APP_INFO()`), and include the copy. Nothing else needs to change. The copy can share a file with NimBLE, `<syslog.h>` and `logging_short.h`.

### Upgrading from 2.x

3.0.0 removed the short names from `logging.h`. In each file that uses `LOG_LEVEL_*` or `LOG_INFO()` and the others, either include `logging_short.h` instead of `logging.h`, or rename to `LOGGING_LEVEL_*` and `LOGGING_INFO()`. `LOGGING_NO_SHORT_NAMES` no longer exists; defining it has no effect.

## API Reference

### Initialization and Cleanup

```c
// Create platform resources (mutex on STM32 FreeRTOS and ESP32)
void log_platform_init(void);
void log_platform_deinit(void);

// Initialize with backend configuration array (a previous set is released first)
void log_init(log_backend_cfg_t *backends, size_t count);

// Deinitialize and release resources (closes files, calls driver deinit)
void log_deinit(void);

// true between log_init() and log_deinit()
bool log_is_initialized(void);
```

Call `log_init()` before other threads log, and stop them before `log_deinit()`. Both functions take the logging lock.

### Backend Control

```c
// Enable/disable backend at runtime
void log_backend_enable(size_t index, bool enabled);

// Set backend log level at runtime
void log_backend_set_level(size_t index, int level);

// Query backend state
bool log_backend_is_enabled(size_t index);
int log_backend_get_level(size_t index);

// Per-backend format control
void log_backend_set_timestamp_format(size_t index, log_timestamp_type_t type);
void log_backend_set_timestamp_formatter(size_t index, log_timestamp_formatter_t formatter);
void log_backend_set_file_line_mode(size_t index, log_file_line_mode_t mode);
log_timestamp_type_t log_backend_get_timestamp_format(size_t index);
log_file_line_mode_t log_backend_get_file_line_mode(size_t index);
```

### Global Timestamp Configuration

```c
// Set built-in timestamp format
void log_set_timestamp_format(log_timestamp_type_t type);

// Set custom timestamp formatter (NULL disables timestamps)
void log_set_timestamp_formatter(log_timestamp_formatter_t formatter);

// Get current format type
log_timestamp_type_t log_get_timestamp_format(void);
```

### Logging Macros

```c
// logging_short.h adds LOG_EMERG() .. LOG_DEBUG() with the same arguments
LOGGING_EMERG(tag, fmt, ...)
LOGGING_ALERT(tag, fmt, ...)
LOGGING_CRIT(tag, fmt, ...)
LOGGING_ERR(tag, fmt, ...)
LOGGING_WARNING(tag, fmt, ...)
LOGGING_NOTICE(tag, fmt, ...)
LOGGING_INFO(tag, fmt, ...)
LOGGING_DEBUG(tag, fmt, ...)
```

`log_write()` (behind the macros) carries the printf format attribute on GCC and Clang, so format strings are checked at compile time. `log_write_cb()` logs a message produced by your own formatter callback, which runs under the lock and writes straight into the message buffer.

### Platform Functions (Port Interface)

```c
uint64_t log_get_timestamp_us(void);      // Monotonic timestamp in microseconds
bool log_lock(unsigned int timeout_ms);   // true when the lock was acquired
void log_unlock(void);
bool log_guard_enter(void);               // false when this thread is already inside a log call
void log_guard_exit(void);
void log_platform_init(void);
void log_platform_deinit(void);
```

- A shipped platform defines all of them. Replacing one of them there causes a duplicate-symbol link error: define `LOGGING_CUSTOM_PLATFORM` and provide your own.
- With `LOGGING_CUSTOM_PLATFORM`, or on a target without a shipped platform, the core supplies weak defaults (timestamp 0, no lock, one guard flag). Override any of them in your application sources. An override placed in a static library may be ignored by the linker, so keep it in an application source file.
- A multi-threaded port must override `log_lock`, `log_unlock`, `log_guard_enter` and `log_guard_exit`. The guard must be per thread.

## Output Format

Log messages are formatted as:

```
[timestamp] LEVEL  [tag] [file:line] message
```

Each bracketed segment is omitted when there is no data for it. Example:
```
[26/09/26-13:56:44.149] INFO   Application started
[26/09/26-13:56:44.456] ERR    [net] Connection failed: timeout
```

With file/line enabled:
```
[26/09/26-13:56:44.149] INFO   [main] [main.c:42] Application started
[26/09/26-13:56:44.456] ERR    [network.c:128] Connection failed: timeout
```

## Complete Example

```c
#include "logging.h"
#include <string.h>

#define LOG_FILENAME "myapp.log"
static char g_log_file_path[256];

static log_backend_cfg_t g_log_backends[] = {
    {
        .type = LOG_OUTPUT_CONSOLE,
        .level = LOGGING_LEVEL_DEBUG,
        .enabled = true,
        .timestamp_format = LOG_TS_TIME_ONLY,
        .file_line_mode = LOG_FILE_LINE_OFF,
    },
    {
        .type = LOG_OUTPUT_FILE,
        .level = LOGGING_LEVEL_DEBUG,
        .enabled = true,
        .config = g_log_file_path,
        .timestamp_format = LOG_TS_DATETIME,
        .file_line_mode = LOG_FILE_LINE_ON,
        .keep_open = true,
    },
};

static void init_log_path(const char *argv0) {
    const char *last_slash = strrchr(argv0, '/');
    if (last_slash) {
        size_t dir_len = (size_t)(last_slash - argv0 + 1);
        if (dir_len < sizeof(g_log_file_path) - sizeof(LOG_FILENAME)) {
            memcpy(g_log_file_path, argv0, dir_len);
            strcpy(g_log_file_path + dir_len, LOG_FILENAME);
            return;
        }
    }
    strcpy(g_log_file_path, LOG_FILENAME);
}

int main(int argc, char *argv[]) {
    init_log_path(argv[0]);
    log_init(g_log_backends, sizeof(g_log_backends) / sizeof(g_log_backends[0]));

    LOGGING_INFO("main", "Application started");
    // Console: [13:56:44.149] INFO   [main] Application started
    // File:    [2026-09-26 13:56:44.149] INFO   [main] [main.c:42] Application started

    log_deinit();
    return 0;
}
```

## Building

### Standalone Build (Linux)

```bash
gcc -o your_app your_app.c \
    src/logging_core.c \
    src/backends/console.c \
    src/backends/file.c \
    src/platforms/linux.c \
    -Isrc -lpthread
```

`console.c` and `file.c` are required on every target. Adding the other platform and backend files is harmless: they compile to nothing on a non-matching target.

### PlatformIO

Add the library as a dependency. All sources are compiled and the matching platform is selected. `examples/esp32_arduino` shows this with `lib_deps = symlink://../..`.

## Compile-Time Configuration

Define these as compiler flags, so that the library sources and your code see the same values:

```c
// Disable all logging: macros become no-ops and arguments are not evaluated
#define LOGGING_DISABLE_LOGGING

// Most verbose level compiled in (default: LOGGING_LEVEL_DEBUG). Calls above it
// compile to nothing. See "Compiled-in Levels".
#define LOGGING_MAX_COMPILED_LEVEL LOGGING_LEVEL_INFO

// Enable file/line information globally (disabled by default)
#define LOGGING_ENABLE_FILE_LINE

// Lock timeout in milliseconds (default: 200)
#define LOGGING_LOCK_TIMEOUT_MS 200

// Message buffer size (default: 1024). The buffer is static and used under
// the logging lock, so its size costs RAM, not task stack.
#define LOG_MESSAGE_BUFFER_SIZE 1024

// Stack buffer for a call that proceeds after a lock timeout (default: 128)
#define LOGGING_UNLOCKED_BUFFER_SIZE 128

// ArduinoLog.h: define only ARDUINO_LOG_LEVEL_*, not LOG_LEVEL_SILENT..VERBOSE
#define LOGGING_ARDUINOLOG_PREFIXED_ONLY

// Timestamp buffer size (default: 32)
#define LOGGING_TIMESTAMP_BUFFER_SIZE 32

// Line prefix buffer of the console and file drivers (default: 128)
#define LOG_PREFIX_BUFFER_SIZE 128

// Use your own platform hooks instead of the shipped platforms
#define LOGGING_CUSTOM_PLATFORM

// STM32: force the FreeRTOS lock, or force the bare-metal lock
#define LOGGING_STM32_FREERTOS
#define LOGGING_STM32_BARE_METAL

// STM32 FreeRTOS: tasks that may be inside the library at once (default: 8)
#define LOGGING_MAX_TASKS 8

// Wall clock for the date/time formats (default: 1 on POSIX and ESP32, else 0)
#define LOGGING_HAVE_WALLCLOCK 1

// Kept for source compatibility only - not used by the library
#define LOGGING_MAX_BACKENDS 4
```

`LOGGING_DISABLE_LOGGING` removes all logging code from your sources. The library functions still exist, as empty bodies when the library also sees the macro. The linker drops them when unused (with `-ffunction-sections` and `--gc-sections`).

## Thread Safety

- One mutex serializes every log call: timestamp, formatting and all driver writes. The message buffer is one static buffer used only while the mutex is held.
- On ESP32 and STM32 with FreeRTOS, call `log_platform_init()` (or `Log.begin()`) before other tasks log. Until then there is no mutex, and concurrent calls can mix their text in the shared buffer (never beyond its end).
- A call waits up to `LOGGING_LOCK_TIMEOUT_MS` for the mutex. On timeout it proceeds without the lock (prevents deadlocks), formatting into a small stack buffer (`LOGGING_UNLOCKED_BUFFER_SIZE`, 128 bytes), so a long message is cut. Its output may then interleave with another call.
- While `log_init()` or `log_deinit()` runs, a call that times out is dropped instead.
- The recursion guard is per thread (POSIX, ESP32) or per task (STM32 FreeRTOS). A log call made from inside a driver or formatter is dropped.
- On STM32 with FreeRTOS, at most `LOGGING_MAX_TASKS` tasks can be inside the library at once (logging or waiting). A call beyond that is dropped.
- Log calls from interrupt handlers are dropped on STM32 FreeRTOS and ESP32. On STM32 bare metal they are allowed, and dropped while the main loop is inside a log call.

## Error Handling

The framework is designed to be fail-safe:

- **Lock timeout**: Proceeds without lock to prevent deadlocks
- **File errors**: Silently ignored (disk full, permissions, etc.)
- **Buffer overflow**: Messages are truncated
- **NULL pointers**: Handled gracefully (a NULL format prints "(null format)")
- **No initialization**: Falls back to console output

## Testing

```bash
tests/host/run.sh                                   # Linux host tests (gcc, AddressSanitizer)
FREERTOS_KERNEL=<FreeRTOS-Kernel> tests/stm32/run.sh          # STM32 builds with arm-none-eabi-gcc
FREERTOS_KERNEL=<FreeRTOS-Kernel> tests/freertos_host/run.sh  # STM32 FreeRTOS port on the POSIX simulator
tests/stack/run.sh                                  # ESP32 stack budget (PlatformIO xtensa toolchain)
```

## Files

```
Logging/
+-- src/
|   +-- logging.h           # Public API (LOGGING_* names only)
|   +-- logging_short.h     # Optional short names LOG_LEVEL_*, LOG_INFO() ...
|   +-- logging_internal.h  # Internal structures, driver helpers
|   +-- logging_platform.h  # Platform selection
|   +-- logging_core.c      # Core implementation
|   +-- logging_format.h/.c # Typed-argument printf formatter
|   +-- ArduinoLog.h/.cpp   # ArduinoLog-compatible interface, Arduino Print driver
|   +-- backends/
|   |   +-- console.c       # Console backend (stdout)
|   |   +-- file.c          # File backend
|   |   +-- serial_stm32.c  # STM32 UART backend
|   |   +-- serial_esp32.c  # ESP32 UART backend
|   +-- platforms/
|       +-- linux.c         # Linux/Unix/macOS
|       +-- stm32.c         # STM32 (HAL + optional FreeRTOS)
|       +-- esp32.c         # ESP32 (ESP-IDF)
+-- examples/
|   +-- esp32_arduino/      # PlatformIO Arduino-ESP32 example
|   +-- stm32/              # STM32 usage examples
+-- tests/                  # Host, STM32 and FreeRTOS simulator tests
+-- README.md
```

## License

(C) 2025 Anatoli Arkhipenko. All rights reserved.
