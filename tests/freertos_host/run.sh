#!/usr/bin/env bash
# Runs the STM32 FreeRTOS port on the FreeRTOS POSIX simulator (Linux host).
# Needs: FREERTOS_KERNEL=<FreeRTOS-Kernel checkout> (tested with V10.6.2)
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
SRC=$(cd "$HERE/../../src" && pwd)
OUT="$HERE/_build"
K=${FREERTOS_KERNEL:?set FREERTOS_KERNEL to a FreeRTOS-Kernel checkout}
rm -rf "$OUT" && mkdir -p "$OUT" && cd "$OUT"
P="$K/portable/ThirdParty/GCC/Posix"
INC="-I$HERE -I$HERE/../stm32/stub -I$SRC -I$K/include -I$P -I$P/utils"
KSRC="$K/tasks.c $K/queue.c $K/list.c $P/port.c $P/utils/wait_for_event.c $K/portable/MemMang/heap_3.c"
LIB="$SRC/logging_core.c $SRC/logging_format.c $SRC/backends/console.c $SRC/backends/file.c $SRC/backends/serial_stm32.c $SRC/platforms/stm32.c"
FAIL=0
for variant in "all:-DEXPECT_ALL=1" "overflow:-DEXPECT_ALL=0 -DLOGGING_MAX_TASKS=2"; do
    name=${variant%%:*}; flags=${variant#*:}
    gcc -std=gnu11 -g -DSTM32F4 $flags $INC -o "t_$name" "$HERE/test_freertos_guard.c" $LIB $KSRC -lpthread \
        || { echo "FAIL build $name"; FAIL=1; continue; }
    timeout 60 "./t_$name" || { echo "FAIL run $name"; FAIL=1; }
done
[ $FAIL -eq 0 ] && echo "FREERTOS HOST TESTS PASSED" || { echo "FREERTOS HOST TESTS FAILED"; exit 1; }
