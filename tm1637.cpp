#include "tm1637.h"

namespace TM1637 {

  /*
     -a-
   |     |
   f     b  dt
   |     |
     -g-
   |     |  dt
   e     c
   |     |
     -d-   

   Bits: [dt][g][f][e][d][c][b][a]
          dt - only for d1
   */

  // Seven-segment patterns for 0-9
  static const uint8_t SEGMENT_MAP[] = {
    0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F
  };

  static const uint8_t SEGMENT_MAP_A = 0x77;
  static const uint8_t SEGMENT_MAP_b = 0x7C;
  static const uint8_t SEGMENT_MAP_C = 0x39;
  static const uint8_t SEGMENT_MAP_c = 0x58;
  static const uint8_t SEGMENT_MAP_d = 0x5E;
  static const uint8_t SEGMENT_MAP_E = 0x79;
  static const uint8_t SEGMENT_MAP_F = 0x71;
  static const uint8_t SEGMENT_MAP_j = 0x1E;
  static const uint8_t SEGMENT_MAP_H = 0x76;
  static const uint8_t SEGMENT_MAP_h = 0x74;
  static const uint8_t SEGMENT_MAP_L = 0x0E;
  static const uint8_t SEGMENT_MAP_n = 0x54;
  static const uint8_t SEGMENT_MAP_o = 0x5C;
  static const uint8_t SEGMENT_MAP_P = 0x73;
  static const uint8_t SEGMENT_MAP_q = 0x67;
  static const uint8_t SEGMENT_MAP_r = 0x50;
  static const uint8_t SEGMENT_MAP_S = 0x6D;
  static const uint8_t SEGMENT_MAP_t = 0x78;
  static const uint8_t SEGMENT_MAP_U = 0x3E;
  static const uint8_t SEGMENT_MAP_u = 0x1C;
  static const uint8_t SEGMENT_MAP_y = 0x6E;
  static const uint8_t SEGMENT_MAP_minus = 0x40;
  static const uint8_t SEGMENT_MAP_underscore = 0x08;
  static const uint8_t SEGMENT_MAP_degree = 0x63;
  
  static bool _isInitialized = false;
  static int _pinClk;
  static int _pinDio;

  void start();
  void stop();
  bool writeByte(uint8_t b);
  
  void init(int pinClk, int pinDio) {
    _pinClk = pinClk;
    _pinDio = pinDio;
    pinMode(_pinClk, OUTPUT);
    pinMode(_pinDio, OUTPUT);
    _isInitialized = true;
  }
  
  // --- Low Level Communication ---
  
  void start() {
    digitalWrite(_pinClk, HIGH);
    digitalWrite(_pinDio, HIGH);
    delayMicroseconds(5); // can be 2 (try it first), same for all 5us
    digitalWrite(_pinDio, LOW);
  }
  
  void stop() {
    digitalWrite(_pinClk, LOW);
    digitalWrite(_pinDio, LOW);
    delayMicroseconds(5);
    digitalWrite(_pinClk, HIGH);
    digitalWrite(_pinDio, HIGH);
    delayMicroseconds(5);
  }
  
  bool writeByte(uint8_t b) {
    for (uint8_t i = 0; i < 8; i++) {
      digitalWrite(_pinClk, LOW);
      digitalWrite(_pinDio, (b & 0x01) ? HIGH : LOW); // Send LSB first
      b >>= 1;
      delayMicroseconds(5);
      digitalWrite(_pinClk, HIGH);
      delayMicroseconds(5);
    }
    
    // Wait for ACK
    digitalWrite(_pinClk, LOW);
    pinMode(_pinDio, INPUT);
    delayMicroseconds(5);
    digitalWrite(_pinClk, HIGH);
    delayMicroseconds(5);
    bool ack = digitalRead(_pinDio) == 0;
    pinMode(_pinDio, OUTPUT);
    return ack;
  }
  
  // --- High Level Functions ---
  
  void _updateDisplay(uint8_t d0, uint8_t d1, uint8_t d2, uint8_t d3, uint8_t brigtness) {
    if (!_isInitialized) return;
  
    // 1. Data Command: Setting automatic address adding
    start();
    writeByte(0x40);
    stop();
  
    // 2. Address Command: Set start address to 0xC0 (first digit)
    start();
    writeByte(0xC0);
    writeByte(d0);
    writeByte(d1);
    writeByte(d2);
    writeByte(d3);
    stop();
  
    // 3. Display Control: Turn on display, set brightness (0x88 is on, 0x8f is max)
    start();
    writeByte(0x88 + (brigtness & 7)); // 0x88 is "ON", +7 is max brightness
    stop();
  }

  void updateDisplay(uint16_t num, bool showColon, bool displayLeadingZeroes, uint8_t brigtness) {
    uint8_t digits[4];
    digits[0] = SEGMENT_MAP[(num / 1000) % 10];
    digits[1] = SEGMENT_MAP[(num / 100) % 10];
    digits[2] = SEGMENT_MAP[(num / 10) % 10];
    digits[3] = SEGMENT_MAP[num % 10];

    for (uint8_t i = 0; i < 3; i++) {
      if (!displayLeadingZeroes && digits[i] == SEGMENT_MAP[0]) digits[i] = 0;
      else break;
    }
  
    // apply colon to second digit
    if (showColon) {
      digits[1] |= 0x80;
    }

    _updateDisplay(digits[0], digits[1], digits[2], digits[3], brigtness);
  }

  void updateDisplayWithError(uint8_t errorCode, uint8_t brigtness) {
    uint8_t errDigit = 0;
    if (errorCode == 0xFF) {
      errDigit = 0;
    } else if (errorCode < 10) {
      errDigit = SEGMENT_MAP[errorCode];
    } else if (errorCode == 0x10) {
      errDigit = SEGMENT_MAP_A;
    } else if (errorCode == 0x11) {
      errDigit = SEGMENT_MAP_C;
    } else if (errorCode == 0x12) {
      errDigit = SEGMENT_MAP_E;
    } else if (errorCode == 0x13) {
      errDigit = SEGMENT_MAP_r;
    }
    
    _updateDisplay(SEGMENT_MAP_E, SEGMENT_MAP_r, SEGMENT_MAP_r, errDigit, brigtness);
  }
  
}
