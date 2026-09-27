/* Link test: every symbol the STM32 port and drivers need must resolve (tests only) */
#include "logging.h"
#include "stm32f4xx_hal.h"

#if defined(LOGGING_STM32_USE_FREERTOS)
#include "FreeRTOS.h"
#include "task.h"
#if (configSUPPORT_STATIC_ALLOCATION == 1)
static StaticTask_t idle_tcb;
static StackType_t idle_stack[configMINIMAL_STACK_SIZE];
void vApplicationGetIdleTaskMemory(StaticTask_t **tcb, StackType_t **stack, uint32_t *size) {
    *tcb = &idle_tcb; *stack = idle_stack; *size = configMINIMAL_STACK_SIZE;
}
#endif
#endif

static UART_HandleTypeDef huart2;
uint32_t HAL_GetTick(void) { return 0; }
HAL_StatusTypeDef HAL_UART_Transmit(UART_HandleTypeDef *h, uint8_t *d, uint16_t n, uint32_t t) {
    (void)h; (void)d; (void)n; (void)t; return HAL_OK;
}

static log_backend_cfg_t backends[] = {
    { .type = LOG_OUTPUT_CUSTOM, .driver = &log_driver_serial_stm32, .level = LOGGING_LEVEL_DEBUG,
      .enabled = true, .config = &huart2, .timestamp_format = LOG_TS_ELAPSED_MS },
};

int main(void) {
    log_platform_init();
    log_init(backends, 1);
    LOGGING_INFO("main", "started %d", 1);
    log_deinit();
    log_platform_deinit();
    return 0;
}
