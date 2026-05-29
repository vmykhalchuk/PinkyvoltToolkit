#ifndef CLOCK_H
#define CLOCK_H

#include <Arduino.h>

class ClockHR {
  public:
    static uint32_t now; // Static variable shared by everyone
    static uint32_t tick() {
      now = millis();
      return now;
    }
    static bool isElapsed(uint32_t since, uint16_t intervalMs) {
      return now - since >= intervalMs;
    }
};

// low resolution version
// Supports time tracking up to 65.5sec
// Do not push to use it for time tracking that requires longer then 60sec periods,
//     give your code 5sec wiggle room to act before timer overflows
class ClockLR {
  public:
    static uint16_t now;
    static uint16_t tick() {
      now = ClockHR::tick();
      return now;
    }
    static bool isElapsed(uint16_t since, uint16_t intervalMs) {
      return now - since >= intervalMs;
    }
    // `lowerIntervalMs` must be << `intervalMs` and the bigger the gap - the better - less time wasted to update `since`
    //   NOTE: `lowerIntervalMs` must be way above your longest interval to not trigger accidentaly some useful action
    static void preventTimerOverrun(uint16_t &since, uint16_t intervalMs, uint16_t lowerIntervalMs) {
      if (isElapsed(since, intervalMs)) {
        since = now - lowerIntervalMs;
      }
    }
};

#endif
