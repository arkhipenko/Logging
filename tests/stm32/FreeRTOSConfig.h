/* FreeRTOS configuration for the STM32 build tests (tests only) */
#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

#ifndef LOGTEST_STATIC
#define LOGTEST_STATIC 1
#endif

#define configUSE_PREEMPTION                    1
#define configCPU_CLOCK_HZ                      168000000
#define configTICK_RATE_HZ                      1000
#define configMAX_PRIORITIES                    5
#define configMINIMAL_STACK_SIZE                128
#define configTOTAL_HEAP_SIZE                   (16 * 1024)
#define configMAX_TASK_NAME_LEN                 16
#define configUSE_16_BIT_TICKS                  0
#define configUSE_MUTEXES                       1
#define configUSE_IDLE_HOOK                     0
#define configUSE_TICK_HOOK                     0
#define configSUPPORT_STATIC_ALLOCATION         LOGTEST_STATIC
#define configSUPPORT_DYNAMIC_ALLOCATION        1
#define configPRIO_BITS                         4
#define configKERNEL_INTERRUPT_PRIORITY         (15 << (8 - configPRIO_BITS))
#define configMAX_SYSCALL_INTERRUPT_PRIORITY    (5 << (8 - configPRIO_BITS))
#define INCLUDE_vTaskDelay                      1

#endif
