/**
  ******************************************************************************
  * @file           : test_freertos_guard.c
  * @brief          : STM32 FreeRTOS port (lock and per-task recursion guard)
  *                   running on the FreeRTOS POSIX simulator
  * @note           : Several tasks log at once while the driver blocks, so
  *                   tasks queue inside the library. No line may be lost.
  ******************************************************************************
  */

#include "logging.h"
#include "stm32f4xx_hal.h"
#include "FreeRTOS.h"
#include "task.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define TASKS     4
#define MESSAGES  300

#ifndef EXPECT_ALL
#define EXPECT_ALL 1
#endif

// HAL and CMSIS stand-ins for the simulator
uint32_t HAL_GetTick(void) { return (uint32_t)xTaskGetTickCount(); }
HAL_StatusTypeDef HAL_UART_Transmit(UART_HandleTypeDef *h, uint8_t *d, uint16_t n, uint32_t t) {
    (void)h; (void)d; (void)n; (void)t; return HAL_OK;
}
static __thread uint32_t g_masked;
uint32_t stub_irq_state(void) { return g_masked; }
void stub_irq_disable(void) { vPortDisableInterrupts(); g_masked = 1; }
void stub_irq_restore(uint32_t state) { g_masked = state; if (!state) { vPortEnableInterrupts(); } }

static StaticTask_t idle_tcb;
static StackType_t idle_stack[configMINIMAL_STACK_SIZE];
void vApplicationGetIdleTaskMemory(StaticTask_t **tcb, StackType_t **stack, uint32_t *size) {
    *tcb = &idle_tcb; *stack = idle_stack; *size = configMINIMAL_STACK_SIZE;
}

// Capture driver: counts lines, blocks now and then, and logs recursively on request
static volatile int g_lines = 0;
static volatile int g_nested_attempts = 0;
static void cap_write(void *internal, const log_backend_cfg_t *cfg,
                      unsigned int level, const char *tag,
                      const char *file, int line,
                      const char *timestamp_str, const char *message) {
    (void)internal; (void)cfg; (void)level; (void)tag; (void)file; (void)line; (void)timestamp_str;
    g_lines++;
    if (strstr(message, "recurse")) {
        g_nested_attempts++;
        LOGGING_ERR("nested", "must be dropped");
    }
    if ((g_lines % 5) == 0) {
        vTaskDelay(1);   // hold the mutex so other tasks queue inside the library
    }
}
static const log_backend_driver_t cap_driver = { "cap", NULL, NULL, cap_write };
static log_backend_cfg_t g_backends[] = {
    { .type = LOG_OUTPUT_CUSTOM, .driver = &cap_driver, .level = LOGGING_LEVEL_DEBUG,
      .enabled = true, .timestamp_format = LOG_TS_ELAPSED_MS },
};

static volatile int g_done = 0;

static void worker(void *arg) {
    int id = (int)(intptr_t)arg;
    for (int i = 0; i < MESSAGES; i++) {
        LOGGING_INFO("w", "task %d message %d", id, i);
    }
    g_done++;
    vTaskDelete(NULL);
}

static void monitor(void *arg) {
    (void)arg;
    while (g_done < TASKS) {
        vTaskDelay(10);
    }
    int before = g_lines;
    LOGGING_INFO("m", "recurse once");
    int ok = 1;
    int expected = 1 + TASKS * MESSAGES;   // pre-scheduler line plus workers
#if EXPECT_ALL
    printf("%s every line from %d concurrent tasks arrived (%d of %d)\n",
           (before == expected) ? "PASS" : "FAIL", TASKS, before, expected);
    ok &= (before == expected);
#else
    printf("%s table overflow drops lines without hanging (%d of %d)\n",
           (before > 1 && before < expected) ? "PASS" : "FAIL", before, expected);
    ok &= (before > 1 && before < expected);
#endif
    printf("%s nested call from the driver is dropped (lines %d -> %d, attempts %d)\n",
           (g_lines == before + 1 && g_nested_attempts == 1) ? "PASS" : "FAIL",
           before, g_lines, g_nested_attempts);
    ok &= (g_lines == before + 1 && g_nested_attempts == 1);
    fflush(stdout);
    exit(ok ? 0 : 1);
}

int main(void) {
    log_platform_init();
    log_init(g_backends, 1);
    LOGGING_INFO("main", "before the scheduler");

    for (int i = 0; i < TASKS; i++) {
        xTaskCreate(worker, "w", configMINIMAL_STACK_SIZE, (void *)(intptr_t)i, 1 + (i % 3), NULL);
    }
    xTaskCreate(monitor, "m", configMINIMAL_STACK_SIZE, NULL, 1, NULL);
    vTaskStartScheduler();
    return 1;
}
