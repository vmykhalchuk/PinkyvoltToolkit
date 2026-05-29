#ifndef SWITCH_PIN_H
#define SWITCH_PIN_H

#include <Arduino.h>

/* 
 *  Example initialization:
 *    SwitchDef sw_InfoPanel_Buzzer = {.pinNo = A5, .isActiveHigh = IS_ACTIVE_HIGH, .isOn = false};
 *    
 *    SwitchDef sw_InfoPanel_Buzzer = {A5, IS_ACTIVE_HIGH, false};
 */
struct SwitchDef {
  const int pinNo;
  const bool isActiveHigh;
  bool isOn; // FIXME When initializing - this field is not taken into account!!!
};

const bool IS_ACTIVE_HIGH = true;
const bool IS_ACTIVE_LOW = false;

void initSwitch(SwitchDef &swDef, bool initInOnState = false);

void switchOn(SwitchDef &swDef);
void switchOff(SwitchDef &swDef);

// Toggle switch toggle to On (if 'on' is true) or Off (if 'on' is false) position
void switchToggleTo(SwitchDef &swDef, bool on);

void toggleSwitch(SwitchDef &swDef);

// example calls:
//  blink(swDef, 5) - blink 5 times, 150ms pulses with 150ms delay after each pulse
//  blink(swDef, 5, 300) - blink 5 times, 300ms pulses with 300ms delay after each pulse
//  blink(swDef, 5, 200, 500) - blink 5 times, 200ms pulses with 500ms delay after each pulse
void blink(SwitchDef &swDef, uint8_t times, unsigned int pulseMs = 150, unsigned int delayMs = 0);

bool isSwitchOn(SwitchDef &swDef);

#endif
