#!/usr/bin/env bash
# Host regression tests for the logging framework (Linux, gcc).
# Usage: tests/host/run.sh   (from any directory)
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
SRC=$(cd "$HERE/../../src" && pwd)
OUT="$HERE/_build"
rm -rf "$OUT" && mkdir -p "$OUT"
cd "$OUT"

CC=${CC:-gcc}
CFLAGS="-std=c11 -Wall -Wextra -Werror -g -I$SRC"
LIB_SRCS="$SRC/logging_core.c $SRC/logging_format.c $SRC/backends/console.c $SRC/backends/file.c $SRC/backends/serial_stm32.c $SRC/backends/serial_esp32.c $SRC/platforms/linux.c $SRC/platforms/stm32.c $SRC/platforms/esp32.c"
FAIL=0
pass() { echo "PASS $1"; }
fail() { echo "FAIL $1"; FAIL=1; }
run() { # name, command...
    local name=$1; shift
    if ! "$@" > "$name.out" 2>&1; then fail "$name exited non-zero"; fi
    grep -E '^(PASS|FAIL|OK:|FAILED:)|Sanitizer|ERROR' "$name.out"
}

echo "== build: library objects and archive =="
OBJS=""
for f in $LIB_SRCS; do
    o=$(basename "$f" .c).o
    $CC $CFLAGS -c "$f" -o "$o" || fail "compile $f"
    OBJS="$OBJS $o"
done
ar rcs liblogging.a $OBJS

echo "== hook binding =="
if nm linux.o | grep -qE ' W log_'; then fail "POSIX port hooks are weak"; else pass "POSIX port hooks are strong"; fi
if nm logging_core.o | grep -qE ' [TW] log_(lock|unlock|get_timestamp_us|guard_enter)$'; then
    fail "core defines hooks on a POSIX target"; else pass "core defines no hooks on a POSIX target"; fi

echo "== functional tests, three link orders =="
$CC $CFLAGS -o t_core_first "$HERE/test_logging.c" logging_core.o console.o file.o linux.o -lpthread && run core_first ./t_core_first || fail "core_first build or run"
$CC $CFLAGS -o t_port_first "$HERE/test_logging.c" linux.o logging_core.o console.o file.o -lpthread && run port_first ./t_port_first || fail "port_first build or run"
$CC $CFLAGS -o t_archive "$HERE/test_logging.c" -L. -llogging -lpthread && run archive ./t_archive || fail "archive build or run"

echo "== default output, no -D flags (wall clock) =="
cat > default.c <<'C'
#include "logging.h"
int main(void) { LOGGING_INFO("app", "default format"); return 0; }
C
$CC $CFLAGS -o t_default default.c -L. -llogging -lpthread && ./t_default > default.out
if grep -qE '^\[[0-9]{2}/[0-9]{2}/[0-9]{2}-[0-9]{2}:[0-9]{2}:[0-9]{2}\.[0-9]{3}\] INFO   \[app\] default format$' default.out; then
    pass "default console line: $(cat default.out)"; else fail "default console line: $(cat -v default.out)"; fi

echo "== generic platform: weak defaults with one override =="
GEN="-DLOGGING_CUSTOM_PLATFORM -DLOGGING_HAVE_WALLCLOCK=0"
$CC $CFLAGS $GEN -c "$SRC/logging_core.c" -o g_core.o
$CC $CFLAGS $GEN -c "$SRC/logging_format.c" -o g_format.o
$CC $CFLAGS $GEN -c "$SRC/backends/console.c" -o g_console.o
$CC $CFLAGS $GEN -c "$SRC/backends/file.c" -o g_file.o
$CC $CFLAGS $GEN -c "$SRC/platforms/linux.c" -o g_linux.o
ar rcs libgeneric.a g_core.o g_format.o g_console.o g_file.o g_linux.o
$CC $CFLAGS $GEN -o t_generic "$HERE/test_custom_platform.c" -L. -lgeneric && run generic ./t_generic || fail "generic build or run"

echo "== ESP32 serial driver with UART stub =="
ESP="-DESP32 -DLOGGING_CUSTOM_PLATFORM -I$HERE/stub"
$CC $CFLAGS $ESP -o t_serial "$HERE/test_serial_esp32.c" "$SRC/logging_core.c" "$SRC/logging_format.c" "$SRC/backends/console.c" "$SRC/backends/file.c" "$SRC/backends/serial_esp32.c" \
    && ./t_serial > serial.out
first=$(head -c 255 serial.out | tail -c 2 | od -An -c | tr -s ' ')
if [ "$(wc -l < serial.out)" -eq 2 ] && [ "$(head -1 serial.out | wc -c)" -eq 255 ] && grep -q '^INFO   \[t\] second line' serial.out; then
    pass "truncated serial line is 255 bytes and ends with CR LF (last bytes:$first)"
else
    fail "serial truncation"; od -c serial.out | tail -5
fi

echo "== typed-argument formatter (ASan, UBSan) =="
SAN="-fsanitize=address,undefined"
$CC $CFLAGS $SAN -o t_format "$HERE/test_format.c" $LIB_SRCS -lpthread && run format ./t_format || fail "format build or run"

echo "== ArduinoLog interface, fake Arduino layer (ASan, UBSan) =="
CXX=${CXX:-g++}
CXXFLAGS="-Wall -Wextra -Werror -g $SAN -DARDUINO=100 -I$HERE/fake_arduino -I$SRC"
for f in $LIB_SRCS; do $CC $CFLAGS $SAN -c "$f" -o "san_$(basename "$f" .c).o"; done
for std in gnu++11 gnu++17; do
    $CXX -std=$std $CXXFLAGS -o "t_arduinolog_$std" "$HERE/test_arduinolog.cpp" "$SRC/ArduinoLog.cpp" san_*.o -lpthread \
        && run "arduinolog_$std" "./t_arduinolog_$std" || fail "ArduinoLog build $std"
done
printf '#include <ArduinoLog.h>\nvolatile unsigned long vul = 1;\nvoid f(void) { Log.notice("x %%d %%lu" CR, 1, vul); Log.begin(ARDUINO_LOG_LEVEL_VERBOSE, NULL); }\n' > disabled.cpp
$CXX -std=gnu++11 $CXXFLAGS -DDISABLE_LOGGING -c disabled.cpp -o disabled.o \
    && $CXX -std=gnu++11 $CXXFLAGS -DDISABLE_LOGGING -c "$SRC/ArduinoLog.cpp" -o disabled_al.o \
    && pass "ArduinoLog.h builds with DISABLE_LOGGING" || fail "ArduinoLog.h with DISABLE_LOGGING"
printf '#include <ArduinoLog.h>\n#include <string>\nvoid f(void) { Log.notice("%%s" CR, std::string("x")); }\n' > unsupported.cpp
if ! $CXX -std=gnu++11 $CXXFLAGS -c unsupported.cpp -o unsupported.o 2> unsupported.err && grep -q 'unsupported argument type' unsupported.err; then
    pass "unsupported argument type is a compile error with a clear message"
else
    fail "unsupported argument type not rejected"
fi

echo "== level names (ArduinoLog.h, NimBLE) =="
NAMES="-std=gnu++11 -Wall -Wextra -DARDUINO=100 -I$HERE/fake_arduino -I$SRC -I$HERE"
$CXX $NAMES $SAN -DLOG_LEVEL=LOG_LEVEL_VERBOSE -o t_dti "$HERE/test_dti_compat.cpp" "$SRC/ArduinoLog.cpp" san_*.o -lpthread \
    && run dti ./t_dti || fail "DTI pattern build"
printf '#include "nimble_like.h"\n#include <ArduinoLog.h>\nint f(void) { return LOG_LEVEL_DEBUG + ARDUINO_LOG_LEVEL_ERROR; }\n' > nimble_first.cpp
printf '#include <ArduinoLog.h>\n#include "nimble_like.h"\nint f(void) { return LOG_LEVEL_DEBUG + ARDUINO_LOG_LEVEL_ERROR; }\n' > nimble_last.cpp
for f in nimble_first nimble_last; do
    if $CXX $NAMES -Werror -DLOGGING_ARDUINOLOG_PREFIXED_ONLY -c $f.cpp -o $f.o 2> $f.err; then
        pass "$f: no warning with LOGGING_ARDUINOLOG_PREFIXED_ONLY"
    else
        fail "$f with LOGGING_ARDUINOLOG_PREFIXED_ONLY"; head -5 $f.err
    fi
    $CXX $NAMES -c $f.cpp -o $f.o 2> $f.err
    w=$(grep -c 'redefined' $f.err); we=$(grep 'redefined' $f.err | grep -c 'LOG_LEVEL_ERROR')
    if [ "$w" -eq 1 ] && [ "$we" -eq 1 ]; then pass "$f: only LOG_LEVEL_ERROR overlaps (as with ArduinoLog 1.0.3)"; else fail "$f: $w redefinitions"; grep redefined $f.err | head -5; fi
done
expect_error() { # name, message part, compiler and flags...
    local name=$1 msg=$2; shift 2
    if ! "$@" -c $name -o /dev/null 2> $name.err && grep -q "$msg" $name.err; then return 0; fi
    head -5 $name.err; return 1
}
for order in "logging.h ArduinoLog.h" "ArduinoLog.h logging.h"; do
    set -- $order
    printf '#include <%s>\n#include <%s>\nint f(void) { return LOGGING_LEVEL_DEBUG + LOG_LEVEL_VERBOSE; }\n' $1 $2 > al_order.cpp
    $CXX $NAMES -Werror -c al_order.cpp -o al_order.o && pass "$1 then $2: no warning" || fail "$1 then $2"
done
printf '#include <logging_short.h>\n#include <ArduinoLog.h>\n' > short_then_al.cpp
expect_error short_then_al.cpp 'logging_short.h is already included' $CXX $NAMES \
    && pass "logging_short.h before ArduinoLog.h stops with a clear #error" || fail "logging_short.h before ArduinoLog.h not rejected"
printf '#include <ArduinoLog.h>\n#include <logging_short.h>\n' > al_then_short.cpp
expect_error al_then_short.cpp 'a LOG_LEVEL_\* name is already defined' $CXX $NAMES \
    && pass "ArduinoLog.h before logging_short.h stops with a clear #error" || fail "ArduinoLog.h before logging_short.h not rejected"

echo "== level names (logging.h, logging_short.h, NimBLE, syslog.h) =="
printf '#include <logging.h>\n#if defined(LOG_INFO) || defined(LOG_LEVEL_INFO) || defined(LOG_LEVEL_EMERG)\n#error short names present\n#endif\nint f(void) { return LOGGING_LEVEL_INFO; }\n' > noshort.c
$CC $CFLAGS -c noshort.c -o noshort.o && pass "logging.h defines no short names" || fail "logging.h defines short names"
for order in "logging.h nimble_like.h" "nimble_like.h logging.h"; do
    set -- $order
    printf '#include "%s"\n#include "%s"\n_Static_assert(LOGGING_LEVEL_DEBUG == 7 && LOG_LEVEL_DEBUG == 0 && LOG_LEVEL_NONE == 5, "values");\nvoid f(void) { LOGGING_DEBUG("t", "x %%d", 1); }\n' $1 $2 > nimble_c.c
    $CC $CFLAGS -I$HERE -c nimble_c.c -o nimble_c.o && pass "$1 then $2: no warning, both value sets intact" || fail "$1 then $2"
done
for order in "logging.h syslog.h" "syslog.h logging.h"; do
    set -- $order
    printf '#include <%s>\n#include <%s>\n_Static_assert(LOGGING_LEVEL_ERR == 3 && LOG_ERR == 3, "values");\nvoid f(void) { LOGGING_ERR("t", "x %%d", 1); }\n' $1 $2 > syslog_c.c
    $CC $CFLAGS -c syslog_c.c -o syslog_c.o && pass "$1 then $2: LOGGING_ERR() works next to the syslog LOG_ERR" || fail "$1 then $2"
done
printf '#include "nimble_like.h"\n#include "logging_short.h"\n' > nimble_then_short.c
expect_error nimble_then_short.c 'a LOG_LEVEL_\* name is already defined' $CC $CFLAGS -I$HERE \
    && pass "NimBLE before logging_short.h stops with a clear #error" || fail "NimBLE before logging_short.h not rejected"
printf '#include <syslog.h>\n#include "logging_short.h"\n' > syslog_then_short.c
expect_error syslog_then_short.c 'a LOG_EMERG .. LOG_DEBUG name is already defined' $CC $CFLAGS \
    && pass "syslog.h before logging_short.h stops with a clear #error" || fail "syslog.h before logging_short.h not rejected"
cat > short.c <<'C'
#include "logging_short.h"
_Static_assert(LOG_LEVEL_NONE == -1 && LOG_LEVEL_EMERG == 0 && LOG_LEVEL_ERR == 3 && LOG_LEVEL_DEBUG == 7 && LOG_LEVEL_MAX == 7, "short level values");
static log_backend_cfg_t be[] = { { .type = LOG_OUTPUT_CONSOLE, .level = LOG_LEVEL_INFO, .enabled = true, .timestamp_format = LOG_TS_NONE } };
int main(void) {
    log_init(be, 1);
    LOG_EMERG("s", "e%d", 0); LOG_ALERT("s", "a"); LOG_CRIT("s", "c"); LOG_ERR("s", "r");
    LOG_WARNING("s", "w"); LOG_NOTICE("s", "n"); LOG_INFO("s", "i%s", "!"); LOG_DEBUG("s", "filtered");
    log_deinit();
    return 0;
}
C
printf 'EMERG  [s] e0\nALERT  [s] a\nCRIT   [s] c\nERR    [s] r\nWARN   [s] w\nNOTICE [s] n\nINFO   [s] i!\n' > short.expected
$CC $CFLAGS -o t_short short.c -L. -llogging -lpthread && ./t_short > short.out && cmp -s short.out short.expected \
    && pass "logging_short.h: all eight LOG_*() calls and LOG_LEVEL_* filter" || { fail "logging_short.h output"; cat -v short.out; }
$CC $CFLAGS -DLOGGING_DISABLE_LOGGING -c short.c -o short_disabled.o && pass "logging_short.h with LOGGING_DISABLE_LOGGING" || fail "logging_short.h disabled build"
sed 's/LOG_/APP_/g' "$SRC/logging_short.h" > app_log.h
grep -q 'LOG_' app_log.h && fail "renamed copy still contains LOG_" || pass "renamed copy contains no LOG_ name"
cat > app_prefix.c <<'C'
#include <syslog.h>
#include "app_log.h"
#include "nimble_like.h"
_Static_assert(APP_LEVEL_DEBUG == 7 && LOG_LEVEL_DEBUG == 0 && LOG_ERR == 3, "values");
int main(void) { APP_INFO("app", "renamed copy %d", APP_LEVEL_DEBUG); return 0; }
C
$CC $CFLAGS -I$HERE -o t_app app_prefix.c -L. -llogging -lpthread && ./t_app | grep -q 'INFO   \[app\] renamed copy 7$' \
    && pass "copy of logging_short.h with LOG_ -> APP_: works next to syslog.h and NimBLE" || fail "renamed copy"
printf '#include "logging_short.h"\n#include "app_log.h"\nint f(void) { LOG_INFO("t", "x"); APP_INFO("t", "y"); return LOG_LEVEL_INFO + APP_LEVEL_INFO; }\n' > both_copies.c
$CC $CFLAGS -c both_copies.c -o both_copies.o && pass "logging_short.h and a renamed copy in one file" || fail "logging_short.h and a renamed copy in one file"

echo "== threads under AddressSanitizer =="
$CC $CFLAGS -fsanitize=address -o t_threads "$HERE/test_threads.c" $LIB_SRCS -lpthread && run threads ./t_threads || fail "threads build or run"

echo "== polling lock (macOS code path) =="
$CC $CFLAGS -DLOGGING_POSIX_NO_TIMEDLOCK -o t_poll "$HERE/test_threads.c" $LIB_SRCS -lpthread && run poll ./t_poll || fail "poll build or run"

echo "== compile checks =="
$CC $CFLAGS -DLOGGING_DISABLE_LOGGING -o t_disabled default.c $LIB_SRCS -lpthread && pass "LOGGING_DISABLE_LOGGING builds" || fail "LOGGING_DISABLE_LOGGING build"
$CC $CFLAGS -DLOGGING_ENABLE_FILE_LINE -o t_fileline default.c $LIB_SRCS -lpthread && ./t_fileline | grep -q '\[app\] \[default.c:2\] default format' \
    && pass "LOGGING_ENABLE_FILE_LINE prints tag and file:line" || fail "LOGGING_ENABLE_FILE_LINE output"
printf '#include "logging.h"\nint main() { LOGGING_INFO("cpp", "x %%d", 1); return 0; }\n' > cpp.cpp
${CXX:-g++} -Wall -Wextra -Werror -I$SRC -c cpp.cpp -o cpp.o && pass "header compiles as C++" || fail "C++ header compile"
printf '#include "logging_short.h"\nint main() { LOG_INFO("cpp", "x %%d", 1); return LOG_LEVEL_INFO; }\n' > cpp_short.cpp
${CXX:-g++} -Wall -Wextra -Werror -I$SRC -c cpp_short.cpp -o cpp_short.o && pass "logging_short.h compiles as C++" || fail "logging_short.h C++ compile"

echo
if grep -h '^FAIL' ./*.out >/dev/null 2>&1 || [ $FAIL -ne 0 ]; then echo "HOST TESTS FAILED"; exit 1; fi
echo "HOST TESTS PASSED"
