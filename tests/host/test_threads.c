/**
  ******************************************************************************
  * @file           : test_threads.c
  * @brief          : Concurrency tests (POSIX port): log_deinit/log_init
  *                   against a writer thread, and the lock timeout
  * @note           : Run under AddressSanitizer
  ******************************************************************************
  */

#define _POSIX_C_SOURCE 200809L

#include "logging.h"
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <time.h>

static int g_failures = 0;

#define CHECK(cond, what)                                                   \
    do {                                                                    \
        if (cond) { printf("PASS %s\n", what); }                            \
        else { printf("FAIL %s (line %d)\n", what, __LINE__); g_failures++; } \
    } while (0)

static log_backend_cfg_t g_backends[] = {
    { .type = LOG_OUTPUT_FILE, .level = LOGGING_LEVEL_DEBUG, .enabled = true,
      .config = "test_threads.log", .keep_open = true, .timestamp_format = LOG_TS_ELAPSED_US },
};

static atomic_int g_stop;

static void *writer(void *arg) {
    (void)arg;
    while (!atomic_load(&g_stop)) {
        LOGGING_INFO("w", "line");
    }
    return NULL;
}

static double now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}

static void *lock_holder(void *arg) {
    (void)arg;
    log_lock(1000);
    struct timespec hold = { 0, 300000000L };
    nanosleep(&hold, NULL);
    log_unlock();
    return NULL;
}

int main(void) {
    // Lifecycle against a concurrent writer: ASan reports any use of a closed FILE
    for (int round = 0; round < 200; round++) {
        atomic_store(&g_stop, 0);
        log_init(g_backends, 1);
        pthread_t t;
        pthread_create(&t, NULL, writer, NULL);
        for (volatile int i = 0; i < 20000; i++) {
        }
        log_deinit();
        atomic_store(&g_stop, 1);
        pthread_join(t, NULL);
    }
    remove("test_threads.log");
    CHECK(1, "200 init/deinit rounds against a writer thread");

    // Lock timeout is honoured
    pthread_t holder;
    pthread_create(&holder, NULL, lock_holder, NULL);
    struct timespec settle = { 0, 50000000L };
    nanosleep(&settle, NULL);
    double t0 = now_ms();
    bool got = log_lock(100);
    double waited = now_ms() - t0;
    CHECK(!got && waited >= 95.0 && waited < 250.0, "log_lock(100) times out after about 100 ms");
    got = log_lock(1000);
    CHECK(got, "log_lock succeeds after the holder releases");
    if (got) {
        log_unlock();
    }
    pthread_join(holder, NULL);

    printf("%s: %d failure(s)\n", g_failures ? "FAILED" : "OK", g_failures);
    return g_failures ? 1 : 0;
}
