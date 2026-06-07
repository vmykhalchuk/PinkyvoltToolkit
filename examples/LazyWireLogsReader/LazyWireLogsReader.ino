#include <pvt_debug.hpp>

constexpr uint8_t PORT_D_LINE_PIN_NO = 3; // 0..7 Pin on PortD
constexpr uint8_t PORT_D_PULL_PIN_NO = 5; // 0..7 Pin on PortD

using ErrorRx = pvt::ErrorReceiver<PORT_D_LINE_PIN_NO, PORT_D_PULL_PIN_NO>;

void setup() {
  Serial.begin(9600);
  Serial.println("Test sketch to read LazyWire");
  delay(1000);
  ErrorRx::setup();
}

void loop() {
  uint8_t err = 0xCC, lastReadByteError = 0xCD;
  bool receivedFrame = ErrorRx::readFrame(0x31, err, lastReadByteError);
  if (err != 0) {
    Serial.print("Error: 0x"); Serial.println(err, HEX);
  }
  if (lastReadByteError != 0) {
    Serial.print("LRB Error: 0x"); Serial.println(lastReadByteError, HEX);
  }
  if (receivedFrame) {
    Serial.print("  ReceivedLength: "); Serial.println(ErrorRx::getReceivedLength());
    Serial.print("  Data[0]=0x"); Serial.println(ErrorRx::getDataByte(0), HEX);
    Serial.print("  Data[1]=0x"); Serial.println(ErrorRx::getDataByte(1), HEX);
    Serial.print("  Data[2]=0x"); Serial.println(ErrorRx::getDataByte(2), HEX);
  }
  delay(2000);
}
