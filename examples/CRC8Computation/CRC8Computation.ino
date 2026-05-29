#include <pvt_crc8.hpp>

void setup() {
  Serial.begin(9600);

  // Example data array
  uint8_t myData[] = {0x01, 0x02, 0x03, 0x04};
  uint8_t dataLength = sizeof(myData);

  // Calculate CRC
  uint8_t crcResult = pvt::CRC8::calculate(myData, dataLength);

  // Print results
  Serial.print("Data: ");
  for(int i=0; i<dataLength; i++) {
    Serial.print(myData[i], HEX);
    Serial.print(" ");
  }
  Serial.print("\nComputed CRC-8: 0x");
  Serial.println(crcResult, HEX);
}

void loop() {
  // Nothing to repeat
}
