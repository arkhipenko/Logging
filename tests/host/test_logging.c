/**
  ******************************************************************************
  * @file           : test_logging.c
  * @brief          : Host regression tests for the logging framework (POSIX port)
  ******************************************************************************
  */

#define _POSIX_C_SOURCE 200809L

#include "logging.h"
#include "logging_internal.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <dirent.h>
#include <time.h>

static int g_failures = 0;

#define CHECK(cond, what)                                                   \
    do {                                                                    \
        if (cond) { printf("PASS %s\n", what); }                            \
        else { printf("FAIL %s (line %d)\n", what, __LINE__); g_failures++; } \
    } while (0)

// *************************************************************************
//  Capture driver: stores the last line built by log_format_line()
// *************************************************************************

static char g_line[1024];
static int g_count = 0;

static void *cap_init(log_backend_cfg_t *cfg) { (void)cfg; return NULL; }
static void cap_deinit(void *internal) { (void)internal; }
static void cap_write(void *internal, const log_backend_cfg_t *cfg,
                      unsigned int level, const char *tag,
                      const char *file, int line,
                      const char *timestamp_str, const char *message) {
    (void)internal;
    (void)cfg;
    log_format_line(g_line, sizeof(g_line), level, tag, file, line,
                    timestamp_str, message, "\n");
    g_count++;
}
static const log_backend_driver_t cap_driver = { "cap", cap_init, cap_deinit, cap_write };

// *************************************************************************
//  Recursive driver: logs and calls a setter from inside write()
// *************************************************************************

static int g_rec_calls = 0;
static void rec_write(void *internal, const log_backend_cfg_t *cfg,
                      unsigned int level, const char *tag,
                      const char *file, int line,
                      const char *timestamp_str, const char *message) {
    (void)internal; (void)cfg; (void)level; (void)tag; (void)file; (void)line;
    (void)timestamp_str; (void)message;
    g_rec_calls++;
    LOGGING_ERR("rec", "nested call must be dropped");
    log_backend_set_file_line_mode(0, LOG_FILE_LINE_OFF);
}
static const log_backend_driver_t rec_driver = { "rec", NULL, NULL, rec_write };

// *************************************************************************
//  Helpers
// *************************************************************************

static double now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}

static int open_fd_count(void) {
    int count = 0;
    DIR *dir = opendir("/proc/self/fd");
    if (!dir) return -1;
    while (readdir(dir)) count++;
    closedir(dir);
    return count;
}

// Check "pattern" where 'd' is any digit, other characters literal
static int matches_digits(const char *s, const char *pattern) {
    for (; *pattern; pattern++, s++) {
        if (*pattern == 'd') { if (!isdigit((unsigned char)*s)) return 0; }
        else if (*s != *pattern) return 0;
    }
    return *s == '\0';
}

// Extract the text between the first '[' and ']'
static void first_bracket(const char *line, char *out, size_t size) {
    out[0] = '\0';
    const char *a = strchr(line, '[');
    const char *b = a ? strchr(a, ']') : NULL;
    if (a && b && (size_t)(b - a - 1) < size) {
        memcpy(out, a + 1, (size_t)(b - a - 1));
        out[b - a - 1] = '\0';
    }
}

static int fmt_no_terminator(uint64_t t, char *buf, size_t size) {
    (void)t;
    memset(buf, 'A', size);   // deliberately no terminator
    return (int)size;
}

// *************************************************************************
//  Tests
// *************************************************************************

static log_backend_cfg_t g_cap_backends[] = {
    { .type = LOG_OUTPUT_CUSTOM, .driver = &cap_driver, .level = LOGGING_LEVEL_INFO,
      .enabled = true, .timestamp_format = LOG_TS_NONE },
};

static void test_filter_and_layout(void) {
    log_init(g_cap_backends, 1);

    g_count = 0;
    LOGGING_DEBUG("t", "filtered");
    CHECK(g_count == 0, "level filter drops DEBUG at INFO");

    LOGGING_INFO("t", "hello %d", 1);
    CHECK(strcmp(g_line, "INFO   [t] hello 1\n") == 0, "layout with tag");

    log_backend_set_level(0, LOGGING_LEVEL_DEBUG);
    g_count = 0;
    LOGGING_DEBUG("t", "now visible");
    CHECK(g_count == 1, "log_backend_set_level raises the fast filter");

    g_count = 0;
    log_write(100, "t", NULL, 0, "level 100");
    log_write(0xFFFFFFFFu, "t", NULL, 0, "huge level");
    CHECK(g_count == 0, "out-of-range levels are not accepted");

    log_backend_set_file_line_mode(0, LOG_FILE_LINE_ON);
    log_write(LOGGING_LEVEL_INFO, "tag", "/a/b/main.c", 42, "x");
    CHECK(strcmp(g_line, "INFO   [tag] [main.c:42] x\n") == 0, "tag and file:line both printed");
    log_write(LOGGING_LEVEL_INFO, NULL, "C:\\src\\net.c", 7, "y");
    CHECK(strcmp(g_line, "INFO   [net.c:7] y\n") == 0, "file:line without tag, backslash path");
    log_backend_set_file_line_mode(0, LOG_FILE_LINE_OFF);

    log_write(LOGGING_LEVEL_INFO, NULL, NULL, 0, NULL);
    CHECK(strcmp(g_line, "INFO   (null format)\n") == 0, "NULL format does not crash");

    log_deinit();
}

static void test_timestamps(void) {
    char ts[64];
    log_init(g_cap_backends, 1);

    log_backend_set_timestamp_format(0, LOG_TS_ELAPSED_US);
    LOGGING_INFO(NULL, "us");
    first_bracket(g_line, ts, sizeof(ts));
    CHECK(ts[0] != '\0' && strcmp(ts, "0") != 0, "ELAPSED_US is non-zero (port hook linked)");

    log_backend_set_timestamp_format(0, LOG_TS_DATETIME_SHORT);
    LOGGING_INFO(NULL, "short");
    first_bracket(g_line, ts, sizeof(ts));
    CHECK(matches_digits(ts, "dd/dd/dd-dd:dd:dd.ddd"), "DATETIME_SHORT is DD/MM/YY-HH:MM:SS.mmm");

    log_backend_set_timestamp_format(0, LOG_TS_DATETIME);
    LOGGING_INFO(NULL, "full");
    first_bracket(g_line, ts, sizeof(ts));
    CHECK(matches_digits(ts, "dddd-dd-dd dd:dd:dd.ddd"), "DATETIME is YYYY-MM-DD HH:MM:SS.mmm");

    log_backend_set_timestamp_format(0, LOG_TS_TIME_ONLY);
    LOGGING_INFO(NULL, "time");
    first_bracket(g_line, ts, sizeof(ts));
    CHECK(matches_digits(ts, "dd:dd:dd.ddd"), "TIME_ONLY is HH:MM:SS.mmm");

    log_backend_set_timestamp_formatter(0, fmt_no_terminator);
    LOGGING_INFO(NULL, "custom");
    first_bracket(g_line, ts, sizeof(ts));
    CHECK(strlen(ts) == LOGGING_TIMESTAMP_BUFFER_SIZE - 1, "unterminated custom formatter is clamped");

    // Global formatter NULL disables timestamps
    log_backend_set_timestamp_format(0, LOG_TS_BACKEND_DEFAULT);
    log_set_timestamp_format(LOG_TS_ELAPSED_MS);
    log_set_timestamp_formatter(NULL);
    CHECK(log_get_timestamp_format() == LOG_TS_NONE, "log_set_timestamp_formatter(NULL) sets NONE");
    LOGGING_INFO(NULL, "no ts");
    CHECK(strcmp(g_line, "INFO   no ts\n") == 0, "no timestamp after formatter NULL");
    log_set_timestamp_format(LOG_TS_DATETIME_SHORT);

    log_backend_set_timestamp_format(0, LOG_TS_NONE);
    log_deinit();
}

static void test_recursion(void) {
    static log_backend_cfg_t be[] = {
        { .type = LOG_OUTPUT_CUSTOM, .driver = &rec_driver, .level = LOGGING_LEVEL_DEBUG,
          .enabled = true, .timestamp_format = LOG_TS_NONE },
    };
    log_init(be, 1);
    double t0 = now_ms();
    LOGGING_INFO("t", "outer");
    double elapsed = now_ms() - t0;
    CHECK(g_rec_calls == 1, "nested log call from a driver is dropped");
    CHECK(elapsed < 50.0, "setter called from a driver does not wait for the lock");
    log_deinit();
}

static void test_reinit_closes_files(void) {
    static log_backend_cfg_t file_be[] = {
        { .type = LOG_OUTPUT_FILE, .level = LOGGING_LEVEL_DEBUG, .enabled = true,
          .config = "test_reinit.log", .keep_open = true, .timestamp_format = LOG_TS_NONE },
    };
    int fds_before = open_fd_count();
    log_init(file_be, 1);
    LOGGING_INFO("t", "first set");
    int fds_open = open_fd_count();
    log_init(g_cap_backends, 1);   // replaces the set without log_deinit()
    int fds_after = open_fd_count();
    CHECK(fds_open == fds_before + 1 && fds_after == fds_before, "second log_init closes the first set");
    log_deinit();
    remove("test_reinit.log");
}

static void test_line_truncation(void) {
    char buf[16];
    size_t len = log_format_line(buf, sizeof(buf), LOGGING_LEVEL_INFO, "tag", NULL, 0, "",
                                 "a message that is far too long", "\r\n");
    CHECK(len == 15 && buf[13] == '\r' && buf[14] == '\n' && buf[15] == '\0',
          "truncated line keeps CR LF");
    len = log_format_line(buf, 2, LOGGING_LEVEL_INFO, NULL, NULL, 0, "", "m", "\r\n");
    CHECK(len == 0 && buf[0] == '\0', "buffer too small for eol yields empty line");
}

int main(void) {
    log_platform_init();
    test_filter_and_layout();
    test_timestamps();
    test_recursion();
    test_reinit_closes_files();
    test_line_truncation();
    log_platform_deinit();

    printf("%s: %d failure(s)\n", g_failures ? "FAILED" : "OK", g_failures);
    return g_failures ? 1 : 0;
}
