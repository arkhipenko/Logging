/**
  ******************************************************************************
  * @file           : test_custom_platform.c
  * @brief          : Generic platform: weak core defaults, partial override,
  *                   64-bit timestamp formatting, wall-clock fallback
  * @note           : Build with -DLOGGING_CUSTOM_PLATFORM -DLOGGING_HAVE_WALLCLOCK=0
  ******************************************************************************
  */

#include "logging.h"
#include "logging_internal.h"
#include <stdio.h>
#include <string.h>

static int g_failures = 0;

#define CHECK(cond, what)                                                   \
    do {                                                                    \
        if (cond) { printf("PASS %s\n", what); }                            \
        else { printf("FAIL %s (line %d)\n", what, __LINE__); g_failures++; } \
    } while (0)

// Override one hook only: the other hooks come from the core weak defaults.
// 5000000000123456 us does not fit in 32 bits.
uint64_t log_get_timestamp_us(void) {
    return 5000000000123456ULL;
}

static char g_ts[64];
static void cap_write(void *internal, const log_backend_cfg_t *cfg,
                      unsigned int level, const char *tag,
                      const char *file, int line,
                      const char *timestamp_str, const char *message) {
    (void)internal; (void)cfg; (void)level; (void)tag; (void)file; (void)line; (void)message;
    snprintf(g_ts, sizeof(g_ts), "%s", timestamp_str);
}
static const log_backend_driver_t cap_driver = { "cap", NULL, NULL, cap_write };

static log_backend_cfg_t g_backends[] = {
    { .type = LOG_OUTPUT_CUSTOM, .driver = &cap_driver, .level = LOG_LEVEL_DEBUG, .enabled = true },
};

int main(void) {
    log_init(g_backends, 1);

    log_backend_set_timestamp_format(0, LOG_TS_ELAPSED_US);
    LOG_INFO(NULL, "x");
    CHECK(strcmp(g_ts, "5000000000123456") == 0, "ELAPSED_US prints all 64 bits");

    log_backend_set_timestamp_format(0, LOG_TS_ELAPSED_SEC);
    LOG_INFO(NULL, "x");
    CHECK(strcmp(g_ts, "5000000000.123456") == 0, "ELAPSED_SEC prints all 64 bits");

    log_backend_set_timestamp_format(0, LOG_TS_ELAPSED_MS);
    LOG_INFO(NULL, "x");
    CHECK(strcmp(g_ts, "5000000000123") == 0, "ELAPSED_MS prints all 64 bits");

    log_backend_set_timestamp_format(0, LOG_TS_DATETIME_SHORT);
    LOG_INFO(NULL, "x");
    CHECK(strcmp(g_ts, "5000000000.123456") == 0, "date/time format falls back to elapsed seconds");

    CHECK(log_lock(10) == true, "weak default log_lock is linked");
    log_unlock();

    log_deinit();
    printf("%s: %d failure(s)\n", g_failures ? "FAILED" : "OK", g_failures);
    return g_failures ? 1 : 0;
}
