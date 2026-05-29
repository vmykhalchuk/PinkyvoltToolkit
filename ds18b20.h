#ifndef DS18B20_H
#define DS18B20_H

#include <Arduino.h>

namespace DS18B20 {

  const float NO_READING = -99.0;

  struct TempSensorDef {
    const int pinNo;
    float temp;
  };

  bool checkSensor(int dataPin);
  
  /*
    The configuration byte follows this pattern: 0 R1 R0 1 1 1 1 1.
    9-bit: 0x1F (00011111)
    10-bit: 0x3F (00111111)
    11-bit: 0x5F (01011111)
    12-bit: 0x7F (01111111)
  */
  bool setResolution(int dataPin, uint8_t hexValue = 0x3F, bool saveToSensorEEPROM = false);

  float readTemperature(int dataPin);
  void readTemperature(TempSensorDef &tempSensor);
}

#endif
