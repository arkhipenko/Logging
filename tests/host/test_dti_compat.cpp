/**
  ******************************************************************************
  * @file           : test_dti_compat.cpp
  * @brief          : DTI edge-devices usage: Logger.h wrapper, level from a
  *                   build flag (-D LOG_LEVEL=LOG_LEVEL_VERBOSE), semLog defined
  *                   by the application
  ******************************************************************************
  */

// The DTI Logger.h wrapper, verbatim
#include <Arduino.h>
#include "ArduinoLog.h"

extern Logging Log;
extern SemaphoreHandle_t   semLog;

#include <stdio.h>
#include <string>

SemaphoreHandle_t semLog = NULL;   // defined by DtiCore.cpp in the application

class CapturePrint : public Print {
  public:
    size_t write(uint8_t c) override { out += (char)c; return 1; }
    std::string out;
};

int main() {
    CapturePrint cap;
    Log.begin(LOG_LEVEL, &cap);    // as DtiCore.cpp
    Log.verbose("verbose %d" CR, 1);
    Log.notice("ip %I" CR, IPAddress(10, 1, 2, 3));
    bool ok = cap.out == "V: verbose 1\nN: ip 10.1.2.3\n";
    printf("%s DTI Logger.h pattern with LOG_LEVEL=LOG_LEVEL_VERBOSE\n", ok ? "PASS" : "FAIL");
    if (!ok) {
        printf("got: %s\n", cap.out.c_str());
    }
    return ok ? 0 : 1;
}
