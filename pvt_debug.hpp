/*
 * Copyright 2026 Volodymyr Mykhalchuk
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef PVT_TOOLKIT_DEBUG_H
#define PVT_TOOLKIT_DEBUG_H

#include <Arduino.h>
#include <wiring.c>
// taps directly into Arduino millis implementation
extern volatile long unsigned int timer0_overflow_count;

#include <pvt_crc8.hpp>

#include "pvt_debug_rx_v2.hpp"
#include "pvt_debug_tx_v2.hpp"

/*
 * Debug tool used to track and report errors in PinkyVolt Projects.
 * It uses single wire to communicate Transmitter with Receiver.
 */

// FIXME Rename to something like LazyWireLogger
namespace pvt::toolkit::debug {

  uint8_t val = *(volatile uint8_t*)&timer0_overflow_count;

  class StateFlags final {
    
    private:
    
      StateFlags() {};
      static uint8_t _f;
      
    public:
    
      inline bool isFlag(uint8_t flagNo) {
        return _f & (1<<flagNo&0x07);
      }
      
      inline void setFlag(uint8_t flagNo) {
        _f |= (1<<flagNo&0x07);
      }

      inline void clearFlag(uint8_t flagNo) {
        _f &= ~(1<<flagNo&0x07);
      }
    
  };

  class Util final {
    public:

      // exampleHowToReadOverflow
      static uint32_t get_overflow_count() {
        uint32_t count;
        uint8_t oldSREG = SREG;  // Save interrupt state
        cli();                   // Disable interrupts
        count = timer0_overflow_count;
        SREG = oldSREG;          // Restore interrupt state
        return count;
      }

      static void checkTimer0Overflow() {
        if (TIFR0 & (1 << TOV0)) {
          // Timer0 just overflowed
          TIFR0 = (1 << TOV0); // Clear the flag (TOV0), however where bit is 0 - that flag will stay untouched
        }
      }

      static uint8_t readTimer0() {
        return TCNT0;
      }

      static void readTimer0Assembly() {
        uint8_t timerVal;
        asm volatile ("in %0, %1" : "=r" (timerVal) : "I" (_SFR_IO_ADDR(TCNT0)));
      }

  };

}

#endif
