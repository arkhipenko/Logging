/**
  ******************************************************************************
  * @file           : logging_short.h
  * @brief          : Optional short names for logging.h
  * @note           : logging.h defines only LOGGING_* names. Include this
  *                   header instead of logging.h in a file that wants the
  *                   short names LOG_LEVEL_* and LOG_EMERG() .. LOG_DEBUG().
  *                   Do not include it in a file that also sees another
  *                   library's names of the same spelling (NimBLE,
  *                   ArduinoLog.h, <syslog.h>): use the LOGGING_* names there.
  *
  *                   Your own prefix: copy this file, replace every "LOG_"
  *                   with your prefix (for example "APP_"), and include the
  *                   copy. Nothing else needs to change.
  ******************************************************************************
  * @attention
  *
  * (C) 2025 Anatoli Arkhipenko
  * This code is provided as-is, without warranty of any kind.
  */

#ifndef LOG_SHORT_NAMES_H
#define LOG_SHORT_NAMES_H

#include "logging.h"

// Stop instead of replacing another library's names (the last definition
// of a macro wins, and gcc only warns)
#if defined(LOG_LEVEL_NONE) || defined(LOG_LEVEL_EMERG) || defined(LOG_LEVEL_ALERT) || \
    defined(LOG_LEVEL_CRIT) || defined(LOG_LEVEL_ERR) || defined(LOG_LEVEL_WARNING) || \
    defined(LOG_LEVEL_NOTICE) || defined(LOG_LEVEL_INFO) || defined(LOG_LEVEL_DEBUG) || \
    defined(LOG_LEVEL_MAX)
#error "logging_short.h: a LOG_LEVEL_* name is already defined by another header (NimBLE, ArduinoLog.h). Use the LOGGING_LEVEL_* names of logging.h in this file."
#endif
#if defined(LOG_EMERG) || defined(LOG_ALERT) || defined(LOG_CRIT) || defined(LOG_ERR) || \
    defined(LOG_WARNING) || defined(LOG_NOTICE) || defined(LOG_INFO) || defined(LOG_DEBUG)
#error "logging_short.h: a LOG_EMERG .. LOG_DEBUG name is already defined by another header (<syslog.h>). Use LOGGING_EMERG() .. LOGGING_DEBUG() of logging.h in this file."
#endif

// Levels
#define LOG_LEVEL_NONE     LOGGING_LEVEL_NONE
#define LOG_LEVEL_EMERG    LOGGING_LEVEL_EMERG
#define LOG_LEVEL_ALERT    LOGGING_LEVEL_ALERT
#define LOG_LEVEL_CRIT     LOGGING_LEVEL_CRIT
#define LOG_LEVEL_ERR      LOGGING_LEVEL_ERR
#define LOG_LEVEL_WARNING  LOGGING_LEVEL_WARNING
#define LOG_LEVEL_NOTICE   LOGGING_LEVEL_NOTICE
#define LOG_LEVEL_INFO     LOGGING_LEVEL_INFO
#define LOG_LEVEL_DEBUG    LOGGING_LEVEL_DEBUG
#define LOG_LEVEL_MAX      LOGGING_LEVEL_MAX

// Logging calls: LOG_INFO(tag, fmt, ...) and so on
#define LOG_EMERG          LOGGING_EMERG
#define LOG_ALERT          LOGGING_ALERT
#define LOG_CRIT           LOGGING_CRIT
#define LOG_ERR            LOGGING_ERR
#define LOG_WARNING        LOGGING_WARNING
#define LOG_NOTICE         LOGGING_NOTICE
#define LOG_INFO           LOGGING_INFO
#define LOG_DEBUG          LOGGING_DEBUG

#endif /* LOG_SHORT_NAMES_H */
