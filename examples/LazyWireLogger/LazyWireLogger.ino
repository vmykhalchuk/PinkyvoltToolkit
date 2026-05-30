#include <pvt_debug.hpp>

constexpr uint8_t PORT_D_PIN_NO = 5; // 0..7 Pin on PortD

using ErrorTx = pvt::ErrorTransmitter<PORT_D_PIN_NO, 3>; // Pin 5; 3 bytes of data

void setup() {
  Serial.begin(9600);
  Serial.println("Test sketch with LazyWire Logger enabled");
  ErrorTx::setup();

  randomSeed(analogRead(A0));
}

void loop() {
  ErrorTx::tick();
  // random delay will not hinder communication
  int randomNumber = random(1, 101);
  delay(randomNumber);
  if (randomNumber % 9 == 0) {
    delay(1000); // even these random long delays cannot hinder protocol
  }
}