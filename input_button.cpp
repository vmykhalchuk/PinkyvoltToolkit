#include "input_button.h"

namespace InputButton {

  void tick(Def &def) {
    Internal &_ctx = def._ctx;

    switch (_ctx.smState) {
      case NOT_INITIALIZED:
        pinMode(def.pinNo, def.enablePullup ? INPUT_PULLUP : INPUT);
        if (def.enablePullup) digitalWrite(def.pinNo, HIGH);
        _ctx.btnState = digitalRead(def.pinNo);
        _ctx.stateChangeTmstmp = _ctx.debounceTmstmp = ClockLR::now;
        _ctx.smState = DEBOUNCE_WAITING;
      break;
      case IDLE:
        if (digitalRead(def.pinNo) != _ctx.btnState) {
          _ctx.debounceTmstmp = ClockLR::now;
          _ctx.smState = DEBOUNCE_WAITING;
        }

        // we need to make sure that timer stays high and not overrun to become low again (important for long press)
        ClockLR::preventTimerOverrun(_ctx.stateChangeTmstmp, TOO_LONG_TIME_FREEZE_MAX, TOO_LONG_TIME_FREEZE_MIN);
      break;
      case DEBOUNCE_WAITING:
        if (ClockLR::isElapsed(_ctx.debounceTmstmp, DEBOUNCE_THRESHOLD_MS)) {
          _ctx.smState = DEBOUNCE_FINISHED;
        }
      break;
      case DEBOUNCE_FINISHED:
        if (_ctx.btnState != digitalRead(def.pinNo)) {
          _ctx.btnState = !_ctx.btnState;
          _ctx.stateChangeTmstmp = ClockLR::now;

          if (isPressed(def)) {
            _ctx.wasPressed = true;
            //_ctx.wasLongPressed = false;
          } else {
            _ctx.wasReleased = true;
          }
        }
        _ctx.smState = IDLE;
      break;
      default:
        _ctx.smState = ERROR;
      break;
    }
  }

  bool isPressed(Def &def) {
    return def._ctx.btnState == def.isActiveHigh;
  }

  bool isLongPressed(Def &def) {
    return isPressed(def) && ClockLR::isElapsed(def._ctx.stateChangeTmstmp, LONG_PRESS_DURATION_MS);
  }

  bool wasPressed(Def &def) {
    Internal &_ctx = def._ctx;
    if (_ctx.wasPressed) {
      _ctx.wasPressed = false;
      return true;
    }
    return false;
  }

  bool wasLongPressed(Def &def) {
    Internal &_ctx = def._ctx;
    if (_ctx.wasLongPressed) {
      _ctx.wasLongPressed = false;//FIXME Also clear wasPressed flag!
      return true;
    }
    bool longPressDetected = isLongPressed(def);// FIXME Wouldn't work: it counts since last state changed - aka when button released not when first pressed!
    if (longPressDetected) {
      _ctx.wasLongPressed = true;
      return true;
    }
    return false;
  }

  bool wasReleased(Def &def) {
    Internal &_ctx = def._ctx;
    if (_ctx.wasReleased) {
      _ctx.wasReleased = false;
      return true;
    }
    return false;
  }

  bool isError(Def &def) {
    return def._ctx.smState == ERROR;
  }

}
