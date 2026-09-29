/* Minimal Arduino API stand-in for host tests of ArduinoLog.h (tests only).
 * Covers what the ArduinoLog interface uses: Print, String (with
 * StringSumHelper), __FlashStringHelper and F(). IPAddress is in
 * IPAddress.h and not included here, as in the n-able core. */
#ifndef FAKE_ARDUINO_H
#define FAKE_ARDUINO_H

#include <stdint.h>
#include <stddef.h>
#include <string>

typedef void *SemaphoreHandle_t;   // FreeRTOS type, provided by the real Arduino-ESP32 core

class __FlashStringHelper;
#define F(s) (reinterpret_cast<const __FlashStringHelper *>(s))

class String {
  public:
    String(const char *s = "") : s_(s ? s : "") {}
    const char *c_str() const { return s_.c_str(); }
    unsigned int length() const { return (unsigned int)s_.size(); }
    std::string s_;
};

class StringSumHelper : public String {
  public:
    StringSumHelper(const String &s) : String(s) {}
};

inline StringSumHelper operator+(const String &l, const String &r) {
    StringSumHelper t(l);
    t.s_ += r.s_;
    return t;
}

class Print {
  public:
    virtual ~Print() {}
    virtual size_t write(uint8_t c) = 0;
    size_t print(const char *s) { size_t n = 0; while (*s) { n += write((uint8_t)*s++); } return n; }
    size_t print(char c) { return write((uint8_t)c); }
    size_t print(const String &s) { return print(s.c_str()); }
};

#endif
