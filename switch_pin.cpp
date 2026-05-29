#include "switch_pin.h"

void initSwitch(SwitchDef &swDef, bool initInOnState) {
  if (initInOnState) {
    switchOn(swDef);
  } else {
    switchOff(swDef);
  }
  pinMode(swDef.pinNo, OUTPUT);
}

void switchOn(SwitchDef &swDef) {
  bool onState = swDef.isActiveHigh;
  digitalWrite(swDef.pinNo, onState ? HIGH : LOW);
  swDef.isOn = true;
}

void switchOff(SwitchDef &swDef) {
  bool offState = !swDef.isActiveHigh;
  digitalWrite(swDef.pinNo, offState ? HIGH : LOW);
  swDef.isOn = false;
}

void switchToggleTo(SwitchDef &swDef, bool on) {
  if (on) switchOn(swDef);
  else switchOff(swDef);
}

void toggleSwitch(SwitchDef &swDef) {
  if (swDef.isOn) switchOff(swDef); else switchOn(swDef);
}

void blink(SwitchDef &swDef, uint8_t times, unsigned int pulseMs, unsigned int delayMs) {
  if (delayMs == 0) delayMs = pulseMs;
  for (uint8_t i = 0; i < times; i++) {
    switchOn(swDef);
    delay(pulseMs);
    switchOff(swDef);
    delay(delayMs);
  }
}

bool isSwitchOn(SwitchDef &swDef) {
  return swDef.isOn;
}
