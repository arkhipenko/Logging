/**
  ******************************************************************************
  * @file           : test_compiled_level_arduinolog.cpp
  * @brief          : LOGGING_MAX_COMPILED_LEVEL with the ArduinoLog interface:
  *                   one call per method with a distinct format string and a
  *                   volatile argument. run.sh builds it with several limits
  *                   and checks the output and the object file.
  ******************************************************************************
  */

#include <ArduinoLog.h>
#include <stdio.h>
#include <string>

class CapturePrint : public Print {
  public:
    size_t write(uint8_t c) override { out += (char)c; return 1; }
    std::string out;
};

static CapturePrint cap;
static volatile unsigned long g_counter = 5;

int main() {
    Log.begin(ARDUINO_LOG_LEVEL_VERBOSE, &cap);
    Log.fatal("fatal_text %lu" CR, g_counter);
    Log.error("error_text %lu" CR, g_counter);
    Log.warning("warning_text %lu" CR, g_counter);
    Log.notice("notice_text %lu" CR, g_counter);
    Log.trace("trace_text %lu" CR, g_counter);
    Log.verbose(F("verbose_text %lu" CR), g_counter);
    fputs(cap.out.c_str(), stdout);
    return 0;
}
