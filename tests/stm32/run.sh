#!/usr/bin/env bash
# Arm Cortex-M build tests: compile and link the STM32 port (against a stub
# HAL), the FreeRTOS Cortex-M port and the core with arm-none-eabi-gcc, and
# check the platform selection rules with the preprocessor.
# Needs: arm-none-eabi-gcc on PATH (or ARM_GCC=...), FREERTOS_KERNEL=<FreeRTOS-Kernel checkout>
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
SRC=$(cd "$HERE/../../src" && pwd)
OUT="$HERE/_build"
rm -rf "$OUT" && mkdir -p "$OUT"
cd "$OUT"
CC=${ARM_GCC:-arm-none-eabi-gcc}
NM=${CC%gcc}nm
K=${FREERTOS_KERNEL:?set FREERTOS_KERNEL to a FreeRTOS-Kernel checkout}
LIB="$SRC/logging_core.c $SRC/logging_format.c $SRC/backends/console.c $SRC/backends/file.c $SRC/backends/serial_stm32.c $SRC/platforms/stm32.c $SRC/platforms/freertos_cm.c"
COMMON="-std=c11 -Os -Wall -Wextra -Werror -ffunction-sections -fdata-sections -I$SRC -I$HERE/stub"
LD="--specs=nano.specs --specs=nosys.specs -Wl,--gc-sections"
FAIL=0

check_log() { # name, log: no warning from the library sources, no emulated TLS
    if grep -qE 'warning: .*(logging|platforms|backends)' "$2"; then echo "FAIL $1 (warnings)"; FAIL=1; return; fi
    if $NM "$1.elf" 2>/dev/null | grep -q emutls; then echo "FAIL $1 uses emutls"; FAIL=1; return; fi
    echo "PASS $1"
}

build() { # name, cpu flags, port dir, extra flags (one-step build, FreeRTOS.h on the include path)
    local name=$1 cpu=$2 port=$3 extra=$4
    local rtos="-I$HERE -I$K/include -I$K/portable/GCC/$port"
    local ksrc="$K/tasks.c $K/queue.c $K/list.c $K/portable/GCC/$port/port.c $K/portable/MemMang/heap_4.c"
    if $CC $cpu $COMMON $rtos $extra -o "$name.elf" "$HERE/main_link.c" $LIB $ksrc $LD > "$name.log" 2>&1; then
        check_log "$name" "$name.log"
    else
        echo "FAIL $name"; grep -E 'error|warning' "$name.log" | grep -v 'is not implemented' | head -10; FAIL=1
    fi
}

M4="-mcpu=cortex-m4 -mthumb -mfloat-abi=hard -mfpu=fpv4-sp-d16"
M0="-mcpu=cortex-m0 -mthumb"

# STM32 with FreeRTOS
build m4_freertos_static  "$M4" ARM_CM4F "-DSTM32F4 -DLOGTEST_STATIC=1"
build m4_freertos_dynamic "$M4" ARM_CM4F "-DSTM32F4 -DLOGTEST_STATIC=0"
build m0_freertos_static  "$M0" ARM_CM0  "-DSTM32F4 -DLOGTEST_STATIC=1"

# FreeRTOS Cortex-M (no vendor target macro): FreeRTOS.h on the include path
build m4_cm_freertos_static  "$M4" ARM_CM4F "-DLOGTEST_STATIC=1"
build m4_cm_freertos_dynamic "$M4" ARM_CM4F "-DLOGTEST_STATIC=0"
build m0_cm_freertos_static  "$M0" ARM_CM0  "-DLOGTEST_STATIC=1"

# FreeRTOS Cortex-M with the "freertos/FreeRTOS.h" layout (n-able): the library
# sees only the parent folder; the kernel and the test program see the usual path
mkdir -p sub && rm -rf sub/freertos && cp -r "$K/include" sub/freertos
name=m4_cm_freertos_subdir; port=ARM_CM4F; : > "$name.log"; objs=""
for f in $LIB; do
    o="sub_$(basename "$f" .c).o"
    $CC $M4 $COMMON -I"$OUT/sub" -I$HERE -I$K/portable/GCC/$port -DLOGTEST_STATIC=1 -c "$f" -o "$o" >> "$name.log" 2>&1 || FAIL=1
    objs="$objs $o"
done
ksrc="$K/tasks.c $K/queue.c $K/list.c $K/portable/GCC/$port/port.c $K/portable/MemMang/heap_4.c"
if $CC $M4 $COMMON -I$HERE -I$K/include -I$K/portable/GCC/$port -DLOGTEST_STATIC=1 -o "$name.elf" "$HERE/main_link.c" $objs $ksrc $LD >> "$name.log" 2>&1 \
    && ! grep -q 'error' "$name.log"; then
    check_log "$name" "$name.log"
else
    echo "FAIL $name"; grep -E 'error|warning' "$name.log" | head -10; FAIL=1
fi

# Bare metal STM32: no FreeRTOS on the include path
for cpu in M4 M0; do
    flags=${!cpu}
    if $CC $flags $COMMON -DSTM32F4 -o "bare_$cpu.elf" "$HERE/main_link.c" $LIB $LD > "bare_$cpu.log" 2>&1 \
        && ! grep -qE 'warning: .*(logging|platforms|backends)' "bare_$cpu.log"; then
        echo "PASS bare_metal_$cpu"
    else
        echo "FAIL bare_metal_$cpu"; grep -E 'error|warning' "bare_$cpu.log" | grep -v 'is not implemented' | head -10; FAIL=1
    fi
done

# Detection: FreeRTOS.h on the include path selects the FreeRTOS lock without USE_FREERTOS
if $NM m4_freertos_static.elf | grep -q xQueueSemaphoreTake; then echo "PASS FreeRTOS detected via __has_include"; else echo "FAIL FreeRTOS detection"; FAIL=1; fi

# Platform selection rules (preprocessor only)
check_select() { # name, expected LOGGING_PLATFORM_* macro, extra flags
    local name=$1 want=$2 extra=$3
    local got
    got=$(echo '#include "logging_platform.h"' | $CC $M4 -I$SRC $extra -dM -E - 2>/dev/null | grep -oE 'LOGGING_PLATFORM_[A-Z0-9_]+ 1' | awk '{print $1}')
    if [ "$got" = "$want" ]; then echo "PASS select $name -> $want"; else echo "FAIL select $name: got '$got', want $want"; FAIL=1; fi
}
mkdir -p sel/flat sel/sub/freertos && : > sel/flat/FreeRTOS.h && : > sel/sub/freertos/FreeRTOS.h
check_select cm_flat_layout      LOGGING_PLATFORM_FREERTOS_CM "-I$OUT/sel/flat"
check_select cm_subdir_layout    LOGGING_PLATFORM_FREERTOS_CM "-I$OUT/sel/sub"
check_select cm_opt_in_flag      LOGGING_PLATFORM_FREERTOS_CM "-DLOGGING_FREERTOS"
check_select cm_disabled         LOGGING_PLATFORM_GENERIC     "-I$OUT/sel/flat -DLOGGING_FREERTOS_DISABLE"
check_select cm_custom_platform  LOGGING_PLATFORM_GENERIC     "-I$OUT/sel/flat -DLOGGING_CUSTOM_PLATFORM"
check_select no_freertos         LOGGING_PLATFORM_GENERIC     ""
check_select stm32_wins          LOGGING_PLATFORM_STM32       "-I$OUT/sel/flat -DSTM32F4"
got=$(echo '#include "logging_platform.h"' | $CC $M4 -I$SRC -I$OUT/sel/sub -dM -E - 2>/dev/null | grep -c 'LOGGING_FREERTOS_INCLUDE_SUBDIR 1')
if [ "$got" = "1" ]; then echo "PASS select subdir spelling recorded"; else echo "FAIL select subdir spelling"; FAIL=1; fi

# The STM32 examples compile against the current API
for ex in "$HERE"/../../examples/stm32/*.c; do
    if $CC $M4 $COMMON -DSTM32F4 -I$HERE -c "$ex" -o "$(basename "$ex" .c).o" > ex.log 2>&1; then
        echo "PASS example $(basename "$ex")"
    else
        echo "FAIL example $(basename "$ex")"; head -10 ex.log; FAIL=1
    fi
done

[ $FAIL -eq 0 ] && echo "STM32 BUILD TESTS PASSED" || { echo "STM32 BUILD TESTS FAILED"; exit 1; }
