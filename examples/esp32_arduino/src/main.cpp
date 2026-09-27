/**
  ******************************************************************************
  * @file           : main.cpp
  * @brief          : ESP32 Arduino logging example
  * @note           : Demonstrates multi-backend logging with Arduino Serial
  ******************************************************************************
  * @attention
  *
  * (C) 2025 Anatoli Arkhipenko
  * This code is provided as-is, without warranty of any kind.
  */

#include <Arduino.h>
#include <inttypes.h>
#include "logging.h"

// External driver defined in logging_arduino.cpp
extern const log_backend_driver_t log_driver_serial_arduino;

// *************************************************************************
//  Logging Configuration
// *************************************************************************

static log_backend_cfg_t g_log_backends[] = {
    {
        .type = LOG_OUTPUT_CUSTOM,
        .driver = &log_driver_serial_arduino,
        .level = LOGGING_LEVEL_DEBUG,      // Log all levels
        .enabled = true,
        .config = NULL,                     // Use Serial (USB)
        .timestamp_format = LOG_TS_ELAPSED_MS,  // Show milliseconds since boot
        .custom_ts_formatter = NULL,
        .file_line_mode = LOG_FILE_LINE_OFF,
        .keep_open = false,
        ._internal = NULL,
    },
};

// *************************************************************************
//  Application
// *************************************************************************

void setup() {
    // Initialize Serial
    Serial.begin(115200);
    while (!Serial) {
        delay(10);  // Wait for Serial to connect
    }

    // Initialize logging: mutex first, then the backends
    log_platform_init();
    log_init(g_log_backends, sizeof(g_log_backends) / sizeof(g_log_backends[0]));

    LOGGING_INFO("app", "ESP32 Arduino Logging Example Started");
    LOGGING_INFO("app", "Chip model: %s, cores: %d", ESP.getChipModel(), ESP.getChipCores());
    LOGGING_INFO("app", "Free heap: %" PRIu32 " bytes", ESP.getFreeHeap());
}

void loop() {
    static uint32_t counter = 0;
    static uint32_t last_log = 0;

    uint32_t now = millis();

    // Log every 2 seconds
    if (now - last_log >= 2000) {
        last_log = now;
        counter++;

        // Demonstrate different log levels
        switch (counter % 5) {
            case 0:
                LOGGING_DEBUG("loop", "Debug message #%" PRIu32 " - detailed info", counter);
                break;
            case 1:
                LOGGING_INFO("loop", "Info message #%" PRIu32 " - normal operation", counter);
                break;
            case 2:
                LOGGING_NOTICE("loop", "Notice #%" PRIu32 " - something noteworthy", counter);
                break;
            case 3:
                LOGGING_WARNING("loop", "Warning #%" PRIu32 " - potential issue", counter);
                break;
            case 4:
                LOGGING_ERR("loop", "Error #%" PRIu32 " - something went wrong", counter);
                break;
        }

        // Also log heap status periodically
        if (counter % 5 == 0) {
            LOGGING_INFO("heap", "Free: %" PRIu32 ", Min free: %" PRIu32,
                     ESP.getFreeHeap(), ESP.getMinFreeHeap());
        }
    }

    delay(100);
}
