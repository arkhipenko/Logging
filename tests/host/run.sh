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
int main(void) { LOG_INFO("app", "default format"); return 0; }
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

echo "== level names (ArduinoLog, NimBLE, syslog short names) =="
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
printf '#include <logging.h>\n#include <ArduinoLog.h>\n' > syslog_first.cpp
if ! $CXX $NAMES -c syslog_first.cpp -o syslog_first.o 2> syslog_first.err && grep -q 'short level names are already defined' syslog_first.err; then
    pass "logging.h before ArduinoLog.h stops with a clear #error"
else
    fail "logging.h before ArduinoLog.h not rejected"
fi
printf '#include <logging.h>\nint f(void) { return LOGGING_LEVEL_INFO; }\n#ifdef LOG_INFO\n#error short names present\n#endif\n' > noshort.c
$CC $CFLAGS -DLOGGING_NO_SHORT_NAMES -c noshort.c -o noshort.o && pass "LOGGING_NO_SHORT_NAMES hides the short names" || fail "LOGGING_NO_SHORT_NAMES"

echo "== threads under AddressSanitizer =="
$CC $CFLAGS -fsanitize=address -o t_threads "$HERE/test_threads.c" $LIB_SRCS -lpthread && run threads ./t_threads || fail "threads build or run"

echo "== polling lock (macOS code path) =="
$CC $CFLAGS -DLOGGING_POSIX_NO_TIMEDLOCK -o t_poll "$HERE/test_threads.c" $LIB_SRCS -lpthread && run poll ./t_poll || fail "poll build or run"

echo "== compile checks =="
$CC $CFLAGS -DLOGGING_DISABLE_LOGGING -o t_disabled default.c $LIB_SRCS -lpthread && pass "LOGGING_DISABLE_LOGGING builds" || fail "LOGGING_DISABLE_LOGGING build"
$CC $CFLAGS -DLOGGING_ENABLE_FILE_LINE -o t_fileline default.c $LIB_SRCS -lpthread && ./t_fileline | grep -q '\[app\] \[default.c:2\] default format' \
    && pass "LOGGING_ENABLE_FILE_LINE prints tag and file:line" || fail "LOGGING_ENABLE_FILE_LINE output"
printf '#include "logging.h"\nint main() { LOG_INFO("cpp", "x %%d", 1); return 0; }\n' > cpp.cpp
${CXX:-g++} -Wall -Wextra -Werror -I$SRC -c cpp.cpp -o cpp.o && pass "header compiles as C++" || fail "C++ header compile"

echo
if grep -h '^FAIL' ./*.out >/dev/null 2>&1 || [ $FAIL -ne 0 ]; then echo "HOST TESTS FAILED"; exit 1; fi
echo "HOST TESTS PASSED"
