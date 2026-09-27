/* FreeRTOS configuration for the POSIX simulator test (tests only) */
#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

#include <stdio.h>
#include <stdlib.h>

#define configUSE_PREEMPTION                    1
#define configTICK_RATE_HZ                      1000
#define configMAX_PRIORITIES                    6
#define configMINIMAL_STACK_SIZE                ((unsigned short)16384)
#define configMAX_TASK_NAME_LEN                 16
#define configUSE_16_BIT_TICKS                  0
#define configUSE_MUTEXES                       1
#define configUSE_IDLE_HOOK                     0
#define configUSE_TICK_HOOK                     0
#define configUSE_TIMERS                        0
#define configSUPPORT_STATIC_ALLOCATION         1
#define configSUPPORT_DYNAMIC_ALLOCATION        1
#define configTOTAL_HEAP_SIZE                   (1024 * 1024)
#define INCLUDE_vTaskDelay                      1
#define INCLUDE_vTaskDelete                     1
#define configASSERT(x) do { if (!(x)) { printf("ASSERT %s:%d\n", __FILE__, __LINE__); abort(); } } while (0)

#endif
