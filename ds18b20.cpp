#include <clock.h>
#include "ds18b20.h"

namespace DS18B20 {

  // --- 1-Wire Low Level Functions ---
  
  static bool ds_reset(int dataPin) {
    noInterrupts();
    pinMode(dataPin, OUTPUT);
    digitalWrite(dataPin, LOW);
    delayMicroseconds(480); // Reset pulse
    pinMode(dataPin, INPUT);
    delayMicroseconds(70);  // Wait for presence pulse
    bool presence = !digitalRead(dataPin);
    interrupts();
    delayMicroseconds(410); // Finish the time slot
    return presence;
  }
  
  static void ds_write_bit(int dataPin, uint8_t bit) {
    pinMode(dataPin, OUTPUT);
    digitalWrite(dataPin, LOW);
    if (bit) {
      delayMicroseconds(10); 
      pinMode(dataPin, INPUT);
      delayMicroseconds(55);
    } else {
      delayMicroseconds(65);
      pinMode(dataPin, INPUT);
      delayMicroseconds(5);
    }
  }
  
  static void ds_write_byte(int dataPin, uint8_t data) {
    noInterrupts();
    for (uint8_t i = 0; i < 8; i++) {
      ds_write_bit(dataPin, data & 0x01);
      data >>= 1;
    }
    interrupts();
  }
  
  static uint8_t ds_read_bit(int dataPin) {
    uint8_t bit = 0;
    pinMode(dataPin, OUTPUT);
    digitalWrite(dataPin, LOW);
    delayMicroseconds(3);
    pinMode(dataPin, INPUT);
    delayMicroseconds(10);
    if (digitalRead(dataPin)) bit = 1;
    delayMicroseconds(50);
    return bit;
  }
  
  static uint8_t ds_read_byte(int dataPin) {
    noInterrupts();
    uint8_t data = 0;
    for (uint8_t i = 0; i < 8; i++) {
      if (ds_read_bit(dataPin)) data |= (1 << i);
    }
    interrupts();
    return data;
  }
  
  // --- High Level Temperature Conversion ---
  
  float readTemperature(int dataPin) {
    if (!ds_reset(dataPin)) return NO_READING;
  
    ds_write_byte(dataPin, 0xCC); // Skip ROM (only works with one sensor)
    ds_write_byte(dataPin, 0x44); // Start Conversion
  
    // Wait for conversion (750ms for 12-bit)
    // While converting, the sensor pulls the bus low. 
    // We'll just wait for it to return to high.
    uint16_t start = ClockLR::tick();
    while (!ds_read_bit(dataPin)) {
      ClockLR::tick();
      if (ClockLR::isElapsed(start, 900)) return NO_READING; // Timeout
    }
  
    if (!ds_reset(dataPin)) return NO_READING;
    ds_write_byte(dataPin, 0xCC); // Skip ROM
    ds_write_byte(dataPin, 0xBE); // Read Scratchpad
  
    uint8_t lowByte = ds_read_byte(dataPin);
    uint8_t highByte = ds_read_byte(dataPin);
  
    // Combine bytes into a 16-bit signed integer
    int16_t raw = (highByte << 8) | lowByte;
    // FIXME check if raw == 0x0550 (85C) - this is fallback temperature (sensor browned-out during conversion or is not ready yet)
    
    // Convert to Celsius: DS18B20 uses 0.0625°C per LSB
    return (float)raw * 0.0625;
  }

  void readTemperature(TempSensorDef &tempSensor) {
    tempSensor.temp = readTemperature(tempSensor.pinNo);
  }


  bool checkSensor(int dataPin) {
    return ds_reset(dataPin);
  }
  
  bool setResolution(int dataPin, uint8_t hexValue, bool saveToSensorEEPROM) {
    bool isPresent = ds_reset(dataPin);
    if (isPresent) {
      ds_write_byte(dataPin, 0xCC); // Skip ROM (talk to all/single sensor)
      ds_write_byte(dataPin, 0x4E); // Write Scratchpad command
      
      ds_write_byte(dataPin, 0x00); // TH Alarm (unused, but must be written)
      ds_write_byte(dataPin, 0x00); // TL Alarm (unused, but must be written)
      ds_write_byte(dataPin, hexValue); // The Configuration Byte (Resolution)
      
      // Optional: Save to EEPROM so it survives power loss
      if (saveToSensorEEPROM) {
        ds_reset(dataPin);
        ds_write_byte(dataPin, 0xCC);
        ds_write_byte(dataPin, 0x48); // Copy Scratchpad to EEPROM
        delay(10);           // Wait for EEPROM write
      }
    }
    return isPresent;
  }

}
