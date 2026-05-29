#ifndef PVT_TOOLKIT_CRC8_CALC_H
#define PVT_TOOLKIT_CRC8_CALC_H

#include <Arduino.h>

namespace pvt::toolkit::crc {

  // Example usage:
  // uint8_t data[SIZE]
  // uint8_t crc8 = CRC8::calculate(data, SIZE);
  class CRC8 final {
    private:
      CRC8() {};
      
    public:
    
      // CRC-8 Calculation (Polynomial: 0x07)
      static uint8_t calculate(uint8_t *data, size_t len) {
        
        uint8_t crc = 0x00; // Initial value (Some protocols start with 0xFF)
        
        for (size_t i = 0; i < len; i++) {
          
          crc ^= data[i]; // XOR the next byte into the register
          
          for (uint8_t j = 0; j < 8; j++) {
            if (crc & 0x80) { // If the MSB is 1
                              // the most common CRC-8 polynomial: x^8 + x^2 + x^1 + 1,
                              // which is represented by the hex value 0x07.
                              // 0x07 <- ATM/SMBus standard; 0x31 <- Dallas/Maxim
              crc = (crc << 1) ^ 0x07; // Shift left and XOR with polynomial
            } else {
              crc <<= 1; // Just shift left
            }
          }
        }
        return crc;
      }
  };
}

namespace pvt {
  using CRC8 = pvt::toolkit::crc::CRC8;
}

#endif
