/**
  ******************************************************************************
  * @file           : test_compiled_level.c
  * @brief          : LOGGING_MAX_COMPILED_LEVEL: one call per level with a
  *                   distinct format string and an argument with a side
  *                   effect. run.sh builds it with several limits and checks
  *                   the output, the evaluation count and the object file.
  ******************************************************************************
  */

#include "logging.h"
#include <stdio.h>

static int g_evaluated = 0;

static int side_effect(void) { return ++g_evaluated; }

static log_backend_cfg_t be[] = {
    { .type = LOG_OUTPUT_CONSOLE, .level = LOGGING_LEVEL_DEBUG, .enabled = true, .timestamp_format = LOG_TS_NONE }
};

int main(void) {
    // Used only by the DEBUG call: no unused warning when that call is compiled out
    int debug_only = 7;

    log_init(be, 1);
    LOGGING_EMERG("c", "lvl0_text %d", side_effect());
    LOGGING_ALERT("c", "lvl1_text %d", side_effect());
    LOGGING_CRIT("c", "lvl2_text %d", side_effect());
    LOGGING_ERR("c", "lvl3_text %d", side_effect());
    LOGGING_WARNING("c", "lvl4_text %d", side_effect());
    LOGGING_NOTICE("c", "lvl5_text %d", side_effect());
    LOGGING_INFO("c", "lvl6_text %d", side_effect());
    LOGGING_DEBUG("c", "lvl7_text %d", debug_only + side_effect());
    log_deinit();
    printf("evaluated %d\n", g_evaluated);
    return 0;
}
