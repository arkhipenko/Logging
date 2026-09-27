/* Minimal Arduino API stand-in for host tests of ArduinoLog.h (tests only).
 * Covers what the ArduinoLog interface uses: Print, String (with
 * StringSumHelper), IPAddress, __FlashStringHelper and F(). */
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

class IPAddress {
  public:
    IPAddress(uint8_t a, uint8_t b, uint8_t c, uint8_t d) { b_[0] = a; b_[1] = b; b_[2] = c; b_[3] = d; }
    uint8_t operator[](int i) const { return b_[i]; }
  private:
    uint8_t b_[4];
};

class Print {
  public:
    virtual ~Print() {}
    virtual size_t write(uint8_t c) = 0;
    size_t print(const char *s) { size_t n = 0; while (*s) { n += write((uint8_t)*s++); } return n; }
    size_t print(char c) { return write((uint8_t)c); }
    size_t print(const String &s) { return print(s.c_str()); }
};

#endif
