#!/usr/bin/env bash
# Stack budget check on ESP32: compiles the core, the formatter, the console
# driver and the ArduinoLog interface with the ESP32 compiler and -fstack-usage,
# then sums the measured frames along the ArduinoLog call chains.
# Needs: xtensa-esp32-elf-gcc (PlatformIO toolchain-xtensa-esp-elf), or XTENSA_BIN=<its bin folder>
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
SRC=$(cd "$HERE/../../src" && pwd)
OUT="$HERE/_build"
X=${XTENSA_BIN:-$HOME/.platformio/packages/toolchain-xtensa-esp-elf/bin}
[ -x "$X/xtensa-esp32-elf-gcc" ] || { echo "SKIP: $X/xtensa-esp32-elf-gcc not found"; exit 0; }
rm -rf "$OUT" && mkdir -p "$OUT" && cd "$OUT"

FLAGS="-mlongcalls -Os -fstack-usage -DLOGGING_CUSTOM_PLATFORM -I$SRC"
for f in logging_core.c logging_format.c backends/console.c; do
    "$X/xtensa-esp32-elf-gcc" $FLAGS -c "$SRC/$f" -o "$(basename "$f" .c).o" || { echo "FAIL compile $f"; exit 1; }
done
"$X/xtensa-esp32-elf-g++" $FLAGS -std=gnu++17 -fno-exceptions -fno-rtti -DARDUINO=100 \
    -I"$HERE/../host/fake_arduino" -c "$SRC/ArduinoLog.cpp" -o ArduinoLog.o || { echo "FAIL compile ArduinoLog.cpp"; exit 1; }

python3 - <<'PY'
import glob, re, sys
frames = {}
for f in glob.glob('*.su'):
    for line in open(f):
        where, size, _kind = line.rstrip('\n').split('\t')
        name = re.sub(r'.*:\d+:\d+:', '', where)
        name = re.sub(r'\(.*', '', name).replace('void ', '').strip().split('.')[0]
        frames[name] = max(frames.get(name, 0), int(size))

def total(chain):
    return sum(frames.get(n, 0) for n in chain)

# Budgets in bytes (ESP32, -Os). Measured 2026-09-26: 640 and 320.
checks = [
    ("ArduinoLog call, deepest point while formatting an integer", 700,
     ['Logging::write', 'log_write_values', 'log_write_cb', 'log_dispatch', 'log_emit',
      'log_args_formatter', 'log_format_values', 'emit_signed', 'emit_integer', 'out_repeat']),
    ("ArduinoLog call, Print driver (Serial write chain not included)", 384,
     ['Logging::write', 'log_write_values', 'log_write_cb', 'log_dispatch', 'log_emit',
      'log_arduino_print_write']),
    ("log_dispatch frame (no message buffer on the stack)", 64, ['log_dispatch']),
]
failed = False
for name, budget, chain in checks:
    t = total(chain)
    ok = t <= budget
    failed |= not ok
    print(f"{'PASS' if ok else 'FAIL'} {name}: {t} bytes (budget {budget})")
    print("     " + ", ".join(f"{n} {frames.get(n, 0)}" for n in chain))
sys.exit(1 if failed else 0)
PY
status=$?
[ $status -eq 0 ] && echo "STACK BUDGET PASSED" || echo "STACK BUDGET FAILED"
exit $status
