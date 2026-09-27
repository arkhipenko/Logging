/**
  ******************************************************************************
  * @file           : file.c
  * @brief          : File output backend for logging framework
  * @note           : Simple append-to-file backend, no rotation or size limits.
  *                   Supports keep_open mode for persistent file handle.
  ******************************************************************************
  * @attention
  *
  * (C) 2025 Anatoli Arkhipenko
  * This code is provided as-is, without warranty of any kind.
  */

#include "../logging_internal.h"
#include <stdio.h>

// *************************************************************************
//  File Backend Driver Functions
// *************************************************************************

/**
 * @brief Initialize file backend
 *
 * Opens file if keep_open is true.
 *
 * @param cfg Backend configuration (config field = file path)
 * @return FILE* handle if keep_open and successful, NULL otherwise
 */
void *log_file_driver_init(log_backend_cfg_t *cfg) {
    if (!cfg || !cfg->config || !cfg->keep_open) {
        return NULL;
    }

    const char *file_path = (const char *)cfg->config;

    // Open file in append mode
    FILE *fp = fopen(file_path, "a");
    if (!fp) {
        // Silent failure - log to console if available
        // (but we can't use LOG_* macros here to avoid recursion)
        return NULL;
    }

    return fp;
}

/**
 * @brief Deinitialize file backend
 *
 * Closes file if it was kept open.
 *
 * @param internal FILE* handle from init (may be NULL)
 */
void log_file_driver_deinit(void *internal) {
    if (internal) {
        FILE *fp = (FILE *)internal;
        fflush(fp);
        fclose(fp);
    }
}

// *************************************************************************
//  Internal Helper - Write formatted log line
// *************************************************************************

/**
 * @brief Write formatted log line to file
 *
 * Format: [timestamp] LEVEL  [tag] [file:line] message
 *
 * @param fp FILE* to write to
 * @param level Log level
 * @param tag Module tag (may be NULL)
 * @param file Source file (may be NULL)
 * @param line Source line
 * @param timestamp_str Formatted timestamp
 * @param message Formatted message
 */
static void write_log_line(FILE *fp, unsigned int level, const char *tag,
                           const char *file, int line,
                           const char *timestamp_str, const char *message) {
    char prefix[LOG_PREFIX_BUFFER_SIZE];
    log_format_prefix(prefix, sizeof(prefix), level, tag, file, line, timestamp_str);
    fputs(prefix, fp);
    fputs(message, fp);
    fputc('\n', fp);
}

/**
 * @brief Write log message to file
 *
 * If internal (FILE*) is provided (keep_open mode), uses it directly and flushes.
 * If internal is NULL, opens file from cfg->config path, writes, and closes.
 *
 * @param internal FILE* handle from init (NULL for per-write mode)
 * @param cfg Backend configuration (config field = file path)
 * @param level Log level (0-7)
 * @param tag Module tag (may be NULL)
 * @param file Source file (may be NULL)
 * @param line Source line
 * @param timestamp_str Formatted timestamp
 * @param message Formatted message
 */
void log_file_driver_write(void *internal, const log_backend_cfg_t *cfg,
                           unsigned int level, const char *tag,
                           const char *file, int line,
                           const char *timestamp_str, const char *message) {
    FILE *fp = (FILE *)internal;
    bool close_after = false;

    // If no persistent handle, open file for this write
    if (!fp) {
        if (!cfg || !cfg->config) {
            return;
        }
        const char *file_path = (const char *)cfg->config;
        fp = fopen(file_path, "a");
        if (!fp) {
            return;
        }
        close_after = true;
    }

    // Write the log line
    write_log_line(fp, level, tag, file, line, timestamp_str, message);

    // Flush or close
    if (close_after) {
        fclose(fp);
    } else {
        fflush(fp);
    }
}

// *************************************************************************
//  File Backend Driver Structure
// *************************************************************************

/**
 * @brief File backend driver
 *
 * Provides callbacks for file output. Can be referenced directly
 * when configuring backends, or is used automatically when
 * type == LOG_OUTPUT_FILE and driver == NULL.
 *
 * Configuration:
 *   - cfg->config: (const char *) file path
 *   - cfg->keep_open: if true, file stays open between writes
 */
const log_backend_driver_t log_driver_file = {
    .name   = "file",
    .init   = log_file_driver_init,
    .deinit = log_file_driver_deinit,
    .write  = log_file_driver_write,
};
