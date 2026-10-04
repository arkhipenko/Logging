/**
  ******************************************************************************
  * @file           : arduinolog_serial.ino
  * @brief          : Arduino IDE / arduino-cli example: ArduinoLog interface
  * @note           : Log.begin() installs the Serial backend. LOGGING_*()
  *                   calls go to the same backend with syslog-style levels.
  *                   Needs C++11 and <type_traits> (ESP32; not AVR).
  ******************************************************************************
  * @attention
  *
  * (C) 2025 Anatoli Arkhipenko
  * This code is provided as-is, without warranty of any kind.
  */

#include <ArduinoLog.h>

void setup() {
    Serial.begin(115200);
    while (!Serial) {
        delay(10);  // Wait for Serial to connect
    }

    // Creates the mutex and installs the Serial backend
    Log.begin(LOG_LEVEL_VERBOSE, &Serial);

    Log.notice(F("ArduinoLog example started" CR));
    Log.notice("Build: %s %s" CR, __DATE__, __TIME__);
    LOGGING_INFO("app", "logging.h call through the same backend");
}

void loop() {
    static uint32_t counter = 0;

    counter++;
    Log.verbose("counter=%lu, hex=%04lx" CR, (unsigned long)counter, (unsigned long)counter);
    if (counter % 5 == 0) {
        Log.warning("counter reached a multiple of 5: %d" CR, (int)counter);
    }
    if (counter % 10 == 0) {
        Log.disable();
        Log.error("not printed: output disabled" CR);
        Log.enable();
    }

    delay(1000);
}
