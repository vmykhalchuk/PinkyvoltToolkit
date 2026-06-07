#include <pvt_debug.hpp>

constexpr uint8_t PORT_D_PIN_NO = 5; // 0..7 Pin on PortD

using ErrorTx = pvt::ErrorTransmitter<PORT_D_PIN_NO, 3>; // Pin 5; 3 bytes of data

void setup() {
  Serial.begin(9600);
  Serial.println("Test sketch with LazyWire Logger enabled");
  ErrorTx::setup();
  
  ErrorTx::setErrorFlag(1);
  ErrorTx::setErrorFlag(3);
  ErrorTx::setErrorFlag(5);
  ErrorTx::setErrorFlag(10);

  randomSeed(analogRead(A0));
}

void loop() {
  ErrorTx::tick();
  uint8_t commErr = ErrorTx::getCommunicationError();
  if (commErr != 0) {
    Serial.print("               !!! CommErr: 0x"); Serial.println(commErr, HEX);
  }
  // random delay will not hinder communication
  int randomNumber = random(1, 101);
  delay(randomNumber);
  if (randomNumber % 9 == 0) {
    delay(1000); // even these random long delays cannot hinder protocol
  }
}