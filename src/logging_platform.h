/**
  ******************************************************************************
  * @file           : logging_platform.h
  * @brief          : Compile-time platform selection for logging framework
  * @note           : Private header - included by the core, backends and ports
  ******************************************************************************
  * @attention
  *
  * (C) 2025 Anatoli Arkhipenko
  * This code is provided as-is, without warranty of any kind.
  */

#ifndef __LOGGING_PLATFORM_H
#define __LOGGING_PLATFORM_H

// *************************************************************************
//  Target Detection
// *************************************************************************
//
// LOGGING_TARGET_* names the hardware or OS family. It decides which
// built-in drivers exist (serial_stm32, serial_esp32) and whether a wall
// clock is available.

#if defined(STM32F0) || defined(STM32F1) || defined(STM32F2) || defined(STM32F3) || \
    defined(STM32F4) || defined(STM32F7) || defined(STM32G0) || defined(STM32G4) || \
    defined(STM32H7) || defined(STM32L0) || defined(STM32L1) || defined(STM32L4) || \
    defined(STM32L5) || defined(STM32U5) || defined(STM32WB) || defined(STM32WL)
    #define LOGGING_TARGET_STM32 1
#elif defined(ESP_PLATFORM) || defined(ESP32)
    #define LOGGING_TARGET_ESP32 1
#elif defined(__linux__) || defined(__unix__) || defined(__APPLE__)
    #define LOGGING_TARGET_POSIX 1
#endif

// *************************************************************************
//  Port Selection
// *************************************************************************
//
// Exactly one of the LOGGING_PLATFORM_* macros below is defined as 1.
//
// A shipped port (STM32, ESP32, POSIX) defines every platform hook as a
// strong symbol and the core defines none. With LOGGING_PLATFORM_GENERIC the
// core supplies weak defaults that application code may override one by one.
// Define LOGGING_CUSTOM_PLATFORM to disable the shipped ports.

#if defined(LOGGING_CUSTOM_PLATFORM)
    #define LOGGING_PLATFORM_GENERIC 1
#elif defined(LOGGING_TARGET_STM32)
    #define LOGGING_PLATFORM_STM32 1
#elif defined(LOGGING_TARGET_ESP32)
    #define LOGGING_PLATFORM_ESP32 1
#elif defined(LOGGING_TARGET_POSIX)
    #define LOGGING_PLATFORM_POSIX 1
#else
    #define LOGGING_PLATFORM_GENERIC 1
#endif

// *************************************************************************
//  STM32: FreeRTOS or Bare Metal
// *************************************************************************

#if defined(LOGGING_PLATFORM_STM32) && !defined(LOGGING_STM32_BARE_METAL)
    #if defined(USE_FREERTOS) || defined(LOGGING_STM32_FREERTOS)
        #define LOGGING_STM32_USE_FREERTOS 1
    #elif defined(__has_include)
        #if __has_include("FreeRTOS.h")
            #define LOGGING_STM32_USE_FREERTOS 1
        #endif
    #endif
#endif

// *************************************************************************
//  Wall Clock Availability
// *************************************************************************
//
// 1 when gettimeofday() and localtime_r() are available. The date/time
// timestamp formats fall back to elapsed seconds without a wall clock.

#ifndef LOGGING_HAVE_WALLCLOCK
    #if defined(LOGGING_TARGET_POSIX) || defined(LOGGING_TARGET_ESP32)
        #define LOGGING_HAVE_WALLCLOCK 1
    #else
        #define LOGGING_HAVE_WALLCLOCK 0
    #endif
#endif

// *************************************************************************
//  Weak Symbol Support (core defaults for the generic platform only)
// *************************************************************************

#if defined(__GNUC__) || defined(__clang__) || defined(__CC_ARM)
    #define LOG_WEAK __attribute__((weak))
    #define LOG_HAVE_WEAK 1
#elif defined(__ICCARM__)
    #define LOG_WEAK __weak
    #define LOG_HAVE_WEAK 1
#else
    #define LOG_WEAK
    #define LOG_HAVE_WEAK 0
#endif

#endif /* __LOGGING_PLATFORM_H */
