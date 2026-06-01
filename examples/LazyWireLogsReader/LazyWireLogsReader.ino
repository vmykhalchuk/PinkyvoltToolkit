#include <pvt_debug.hpp>

constexpr uint8_t PORT_D_LINE_PIN_NO = 5; // 0..7 Pin on PortD
constexpr uint8_t PORT_D_PULL_PIN_NO = 6; // 0..7 Pin on PortD

using ErrorRx = pvt::ErrorReceiver<PORT_D_LINE_PIN_NO, PORT_D_PULL_PIN_NO>;

void setup() {
  Serial.begin(9600);
  Serial.println("Test sketch to read LazyWire");
  ErrorRx::setup();
}

void loop() {
  if (ErrorRx::readFrame(0x31)) {
    if (ErrorRx::isReceivedData()) {
      Serial.println("No new Date. Strange Error :(");
    }
    Serial.print("ReceivedLength: "); Serial.println(ErrorRx::getReceivedLength());
    Serial.print("Data[0]="); Serial.println(ErrorRx::getDataByte(0), HEX);
  } else {
    Serial.println("No new Frame");
  }
  delay(2000);
}