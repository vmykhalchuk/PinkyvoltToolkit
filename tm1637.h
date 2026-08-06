#ifndef TM1637_H
#define TM1637_H

#include <Arduino.h>

namespace TM1637 {

  void init(int pinClk, int pinDio);

  void updateDisplay(uint16_t num, bool showColon, bool displayLeadingZeroes = false, uint8_t brigtness = 3/*0..7*/);
  void updateDisplayWithError(uint8_t errorCode/*0..9 or 0x10-A,0x11-C,0x12-E,0x13-r or 0xFF - no code*/ = 0xFF, uint8_t brigtness = 3/*0..7*/);
}
#endif
