/* IPAddress stand-in for host tests of ArduinoLog.h (tests only). Arduino.h
 * does not include it, as in the n-able core, so ArduinoLog.h must. */
#ifndef FAKE_IPADDRESS_H
#define FAKE_IPADDRESS_H

#include <stdint.h>

class IPAddress {
  public:
    IPAddress(uint8_t a, uint8_t b, uint8_t c, uint8_t d) { b_[0] = a; b_[1] = b; b_[2] = c; b_[3] = d; }
    uint8_t operator[](int i) const { return b_[i]; }
  private:
    uint8_t b_[4];
};

#endif
