/**
  ******************************************************************************
  * @file           : test_serial_esp32.c
  * @brief          : ESP32 serial driver against a host UART stub
  * @note           : Build with -DESP32 -DLOGGING_CUSTOM_PLATFORM -Istub
  ******************************************************************************
  */

#include "logging.h"
#include <string.h>

static log_backend_cfg_t g_backends[] = {
    { .type = LOG_OUTPUT_CUSTOM, .driver = &log_driver_serial_esp32, .level = LOGGING_LEVEL_DEBUG,
      .enabled = true, .timestamp_format = LOG_TS_NONE },
};

int main(void) {
    char big[300];
    memset(big, 'x', sizeof(big));
    big[sizeof(big) - 1] = '\0';

    log_init(g_backends, 1);
    LOGGING_INFO("t", "%s", big);      // longer than the 256-byte line buffer
    LOGGING_INFO("t", "second line");
    log_deinit();
    return 0;
}
