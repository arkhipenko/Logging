#!/usr/bin/env bash
# STM32 build tests: compile and link the STM32 port, the STM32 UART driver
# and the core with arm-none-eabi-gcc against a stub HAL.
# Needs: arm-none-eabi-gcc on PATH (or ARM_GCC=...), FREERTOS_KERNEL=<FreeRTOS-Kernel checkout>
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
SRC=$(cd "$HERE/../../src" && pwd)
OUT="$HERE/_build"
rm -rf "$OUT" && mkdir -p "$OUT"
cd "$OUT"
CC=${ARM_GCC:-arm-none-eabi-gcc}
K=${FREERTOS_KERNEL:?set FREERTOS_KERNEL to a FreeRTOS-Kernel checkout}
LIB="$SRC/logging_core.c $SRC/logging_format.c $SRC/backends/console.c $SRC/backends/file.c $SRC/backends/serial_stm32.c $SRC/platforms/stm32.c"
COMMON="-std=c11 -Os -Wall -Wextra -Werror -ffunction-sections -fdata-sections -DSTM32F4 -I$SRC -I$HERE/stub"
LD="--specs=nano.specs --specs=nosys.specs -Wl,--gc-sections"
FAIL=0

build() { # name, cpu flags, port dir, extra flags
    local name=$1 cpu=$2 port=$3 extra=$4
    local rtos="-I$HERE -I$K/include -I$K/portable/GCC/$port"
    local ksrc="$K/tasks.c $K/queue.c $K/list.c $K/portable/GCC/$port/port.c $K/portable/MemMang/heap_4.c"
    if $CC $cpu $COMMON $rtos $extra -o "$name.elf" "$HERE/main_link.c" $LIB $ksrc $LD > "$name.log" 2>&1 \
        && ! grep -qE 'warning: .*(logging|platforms|backends)' "$name.log"; then
        echo "PASS $name"
        # the guard must not use emulated TLS
        if arm-none-eabi-nm "$name.elf" 2>/dev/null | grep -q emutls; then echo "FAIL $name uses emutls"; FAIL=1; fi
    else
        echo "FAIL $name"; grep -E 'error|warning' "$name.log" | grep -v 'is not implemented' | head -10; FAIL=1
    fi
}

M4="-mcpu=cortex-m4 -mthumb -mfloat-abi=hard -mfpu=fpv4-sp-d16"
M0="-mcpu=cortex-m0 -mthumb"
build m4_freertos_static  "$M4" ARM_CM4F "-DLOGTEST_STATIC=1"
build m4_freertos_dynamic "$M4" ARM_CM4F "-DLOGTEST_STATIC=0"
build m0_freertos_static  "$M0" ARM_CM0  "-DLOGTEST_STATIC=1"

# Bare metal: no FreeRTOS on the include path
for cpu in M4 M0; do
    flags=${!cpu}
    if $CC $flags $COMMON -o "bare_$cpu.elf" "$HERE/main_link.c" $LIB $LD > "bare_$cpu.log" 2>&1 \
        && ! grep -qE 'warning: .*(logging|platforms|backends)' "bare_$cpu.log"; then
        echo "PASS bare_metal_$cpu"
    else
        echo "FAIL bare_metal_$cpu"; grep -E 'error|warning' "bare_$cpu.log" | grep -v 'is not implemented' | head -10; FAIL=1
    fi
done

# Detection: FreeRTOS.h on the include path selects the FreeRTOS lock without USE_FREERTOS
if arm-none-eabi-nm m4_freertos_static.elf | grep -q xQueueSemaphoreTake; then echo "PASS FreeRTOS detected via __has_include"; else echo "FAIL FreeRTOS detection"; FAIL=1; fi

# The STM32 examples compile against the current API
for ex in "$HERE"/../../examples/stm32/*.c; do
    if $CC $M4 $COMMON -I$HERE -c "$ex" -o "$(basename "$ex" .c).o" > ex.log 2>&1; then
        echo "PASS example $(basename "$ex")"
    else
        echo "FAIL example $(basename "$ex")"; head -10 ex.log; FAIL=1
    fi
done

[ $FAIL -eq 0 ] && echo "STM32 BUILD TESTS PASSED" || { echo "STM32 BUILD TESTS FAILED"; exit 1; }
