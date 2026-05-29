#ifndef INPUT_BUTTON_H
#define INPUT_BUTTON_H

#include <Arduino.h>
#include "clock.h"

// example:
// // define
// InputButton::Def btnPlus = { .pinNo = 6, .isActiveHigh = false, .enablePullup = true, ._ctx = {} };
// // loop
// InputButton::tick(btnPlus);
// // check
// if (InputButton::wasPressed(btnPlus)) <action here>;
namespace InputButton {

  static const unsigned int DEBOUNCE_THRESHOLD_MS = 50;
  static const unsigned int LONG_PRESS_DURATION_MS = 1000; // TODO Make it configurable via single static variable

  static const unsigned int TOO_LONG_TIME_FREEZE_MAX = 60000; // when timer reaches MAX - it will get updated to now+MIN
  static const unsigned int TOO_LONG_TIME_FREEZE_MIN = 10000; // the bigger the gap - the more efficient it becomes (less often timer will get updated)
  

  enum SMState { NOT_INITIALIZED, IDLE, DEBOUNCE_WAITING, DEBOUNCE_FINISHED, ERROR };
  
  struct Internal {
    bool btnState = false; // FIXME Put all bool as flags to optimize space
    bool wasPressed = false;
    bool wasReleased = false;
    bool wasLongPressed = false;
    uint16_t stateChangeTmstmp = 0;
    uint16_t debounceTmstmp = 0;
    SMState smState = NOT_INITIALIZED;
  };

  struct Def {
    const int pinNo;
    const bool isActiveHigh;
    const bool enablePullup;
    Internal _ctx;
  };

  void tick(Def &def);

  bool isPressed(Def &def);
  bool isLongPressed(Def &def);

  // was/has functions return `true` only once - when calling second time and button is still pressed/released - it returns `false`
  bool wasPressed(Def &def);
  bool wasLongPressed(Def &def);
  bool wasReleased(Def &def);

  //FIXME Implement. Add lastPressedTmstmp into Internal. Add lastPressedLengthMs - to track if last pressed was short or long (If first was a long press - then no double click)
  //bool isDoubleClicked();

  //TODO Add auto-repeat into wasPressed()

  bool isError(Def &def);
}

#endif
