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

#ifndef PVT_TOOLKIT_DEBUG_RX_H
#define PVT_TOOLKIT_DEBUG_RX_H

#include <Arduino.h>
#include <util/atomic.h>
#include <clock.h>
#include <pvt_crc8.hpp>

// FIXME Major rework:
//     1) We need to have Receiver act extremely fast, solution: have it split by two Arduino:
//            a) First Arduino monitors two pins (no interrupts, very tight loop)
//                  -) monitor that signal is transitioning H->L (via M) - and prevents false trigger on M
//                  -) sends data via High speed Serial communication
//                  -) if data comes too fast - slows down Transmitter by stopping at handshake, so Transmitter goes into wait loop
//            b) Receives data via Serial port on high speed
//                  -) Sends all data in HEX Fromat - to make it human readable
//                      S:XX XX XX XX XX XX XX XX XX XX XX
//                      R:XX XX
namespace pvt::toolkit::debug::rx {

  template <uint8_t PORTD_PIN, uint8_t PORTD_PIN_CMP, uint8_t PORTD_PIN_PD, uint8_t SIZE>
  class OneWireErrorReceiver final {
    private:
      OneWireErrorReceiver() {};

      static constexpr uint8_t _SYS_BYTES = 1; // FIXME Implement this logic. 

      static_assert(SIZE >= 1 && SIZE <= 30, "SIZE must be in range [1..30]");
      static_assert(((SIZE + _SYS_BYTES)<<3) <= 0xFF, "SIZE+sysBytes must be addressable with single byte");
      
      static_assert((PORTD_PIN == 2), "PORTD_PIN must be 2");
      static_assert((PORTD_PIN_CMP == 3), "PORTD_PIN must be 3");

      static_assert((PORTD_PIN <= 7), "PORTD_PIN must be within 0..7 range!");
      static_assert((PORTD_PIN_CMP <= 7), "PORTD_PIN_OPAMP must be within 0..7 range!");
      static_assert((PORTD_PIN_PD <= 7), "PORTD_PIN_PD must be within 0..7 range!");

      static_assert((PORTD_PIN != PORTD_PIN_CMP), "PORTD_PIN Must not be same as PORTD_PIN_CMP");
      static_assert((PORTD_PIN != PORTD_PIN_PD), "PORTD_PIN Must not be same as PORTD_PIN_PD");
      static_assert((PORTD_PIN_PD != PORTD_PIN_CMP), "PORTD_PIN_PD Must not be same as PORTD_PIN_CMP");

      static_assert((PORTD_PIN_CMP == PORTD_PIN + 1), "PORTD_PIN_CMP MUST be PORTD_PIN + 1");
      
      enum FSMStateGroup : uint8_t {
        _G_CRIT = 0x10,
        _G_HS   = 0x20,
        _G_RD   = 0x30,
        _G_WR   = 0x40,
        _G_OTH  = 0x50
      };

      enum FSMState : uint8_t {
        _SM_NOP                             = 0,
        
        _SM_CRIT__WAITING_FOR_CONNECTION    = _G_CRIT | 0,
        _SM_CRIT__COMMERROR                 = _G_CRIT | 1, // Communication error

        _SM_HANDSHAKE__S0                   = _G_HS | 0,
        _SM_HANDSHAKE__S1                   = _G_HS | 1,
        _SM_HANDSHAKE__S2                   = _G_HS | 2,
        _SM_HANDSHAKE__S3                   = _G_HS | 3,

        _SM_READ__S0                        = _G_RD | 0,
        _SM_READ__S1                        = _G_RD | 1,
        _SM_READ__S2                        = _G_RD | 2,

        _SM_WRITE_S0                        = _G_WR | 0,

        // this error happens when wrong D2,D3 input (totally unexpected and most likely caused by hardware failure)
        _SM_OTH__ERROR_INPUT_COMBINATION    = _G_OTH | 0,
        _SM_OTH__ALG_ERROR_UNHANDLED_STATE  = _G_OTH | 1,
      };

      inline static void __setDPinToInput()  { asm volatile ("cbi %0, %1" : : "I" (_SFR_IO_ADDR(DDRD)),  "I" (PORTD_PIN) : "memory"); }
      inline static void __setDPinToLow()    { asm volatile ("cbi %0, %1" : : "I" (_SFR_IO_ADDR(PORTD)), "I" (PORTD_PIN) : "memory"); }

      inline static void __setDPinCmpToInput()  { asm volatile ("cbi %0, %1" : : "I" (_SFR_IO_ADDR(DDRD)),  "I" (PORTD_PIN_CMP) : "memory"); }
      inline static void __setDPinCmpToLow()    { asm volatile ("cbi %0, %1" : : "I" (_SFR_IO_ADDR(PORTD)), "I" (PORTD_PIN_CMP) : "memory"); }

      inline static void __setDPinPDToOutput() { asm volatile ("sbi %0, %1" : : "I" (_SFR_IO_ADDR(DDRD)),  "I" (PORTD_PIN_PD) : "memory"); }
      inline static void __setDPinPDToInput()  { asm volatile ("cbi %0, %1" : : "I" (_SFR_IO_ADDR(DDRD)),  "I" (PORTD_PIN_PD) : "memory"); }
      inline static void __setDPinPDToLow()    { asm volatile ("cbi %0, %1" : : "I" (_SFR_IO_ADDR(PORTD)), "I" (PORTD_PIN_PD) : "memory"); }
      inline static void __setDPinPDToHigh()   { asm volatile ("sbi %0, %1" : : "I" (_SFR_IO_ADDR(PORTD)), "I" (PORTD_PIN_PD) : "memory"); }

      enum LineState : uint8_t {
        // NOTE: CMP has 1 when voltage is below its threshold (0.15V) and 0 when above
        M, // PIN_CMP==0 && PIN==0 // Middle (PD+PU)
        H, // PIN_CMP==0 && PIN==1 // High   (PD+AH)|( Z+PU)|( Z+AH)
        L, // PIN_CMP==1 && PIN==0 // Low    (PD+ Z)|(PD+AL)|( Z+AL)
        U, // PIN_CMP==1 && PIN==1 // Unknown (Comparator says voltage is below 0.15V but Pin says that it is HIGH - it contradicts, only Hardware failure can cause this)
        LineStateCount
      };

      struct LineThenState {
        // [NOTE] fields order must adhere to same order as LineState!
        FSMState _m;
        FSMState _h;
        FSMState _l;
        FSMState _u;
      } __attribute__((packed));
  
      inline static LineState _readLine() {
        uint8_t r = PIND >> PORTD_PIN;
        r &= 0x3;
        return (LineState) r;
      }

      /*
             -=- PROTOCOL -=- 

              Handshake:
              M -> L -> M -> H -> M

              Reading 8 bits
                Read 0:
                L -> M
              
                Read 1:
                H -> M

       */

      inline static void _initializePorts() {
        // Main pin to read (no pull-up)
        __setDPinToInput();
        __setDPinToLow();
        // Cmp Pin to read (no pull-up)
        __setDPinCmpToInput();
        __setDPinCmpToLow();

        // Enable PD
        __setDPinPDToOutput();
        __setDPinPDToLow();
      }
      
      inline static void _activatePD() {
        __setDPinPDToOutput();
      }
      inline static void _deactivatePD() {
        __setDPinPDToInput();
      }

      inline static void _suspendCommunication() {
        _activatePD();
        __setDPinPDToHigh();
      }
      
      inline static void _resumeCommunication() {
        __setDPinPDToLow();
        _activatePD();
      }

      //  --- STATUS FLAGS FUNCTIONALITY  ---
      static constexpr uint8_t _SF_IS_CONNECTED = 1<<0;
      static constexpr uint8_t _SF_IS_ERROR = 1<<1;
      static constexpr uint8_t _SF_FRESH_DATA = 1<<2;
      static constexpr uint8_t _SF_WAS_STALLED = 1<<3; // Communication was stalled // FIXME Add support of this Flag (read function and flag set/clear logic)
      static constexpr uint8_t _SF_WDT = 1<<6; // FSM transitions will clear this flag to let tick() reset timer. If though wdt timer elapses - communication is stale - and will be restarted!
      static constexpr uint8_t _SF_COMMERROR_TIMER_ACTIVE = 1<<7; // when comm error happens, coomunication is supsended and timer counts to attempt restart
      static volatile uint8_t _statusFlags; // TODO [PERFORMANCE] Move flags into GPIOR0 register - and use single word commands to modify and test
      static inline bool __isSFSet(uint8_t sf) {
        bool isSet;
        ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
          isSet = _statusFlags & sf;
        }
        return isSet;
      }
      static inline void __setSF(uint8_t sf) {
        ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
          _statusFlags |= sf;
        }
      }
      static inline void __clearSF(uint8_t sf) {
        ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
          _statusFlags &= ~sf;
        }
      }

      // --- MAIN REGISTERS ---

      static volatile uint8_t _receivedData[SIZE + _SYS_BYTES];
      
      static uint8_t _readingDataBuf[SIZE + _SYS_BYTES];
      static uint8_t _readingDataBit;
      static uint8_t _writingDataBuf[2];

      static LineThenState _lineTransitions;
      static uint8_t* const _lineTransitionsAsArr;

      static constexpr uint16_t ERROR_COMMUNICATION_TIMEOUT_MS = 5000;
      static uint16_t _commErrorTimer;

      // When no change in signal happens within this amount of time => communication is reset
      static constexpr uint16_t COMMUNICATION_STALL_TIMEOUT_MS = 2000;
      static uint16_t _wdtTimer;

      // --- MAIN HELPING FUNCTIONS ---
      
      __attribute__((always_inline))
      static inline void __whenLineLMHThen(FSMState _l, FSMState _m, FSMState _h) {
        _lineTransitions._l = _l;
        _lineTransitions._m = _m;
        _lineTransitions._h = _h;
      }

      static inline void __whenLineLThen(FSMState l) {
        _lineTransitions._l = l;
        _lineTransitions._m = _SM_CRIT__COMMERROR;
        _lineTransitions._h = _SM_CRIT__COMMERROR;
      }
      static inline void __whenLineMThen(FSMState m) {
        _lineTransitions._l = _SM_CRIT__COMMERROR;
        _lineTransitions._m = m;
        _lineTransitions._h = _SM_CRIT__COMMERROR;
      }
      static inline void __whenLineHThen(FSMState h) {
        _lineTransitions._l = _SM_CRIT__COMMERROR;
        _lineTransitions._m = _SM_CRIT__COMMERROR;
        _lineTransitions._h = h;
      }
      
      __attribute__((always_inline))
      static inline void __whenLineAnyThen(FSMState s) {
        _lineTransitions._l = s;
        _lineTransitions._m = s;
        _lineTransitions._h = s;
      }

      // --- MAIN LOGIC FUNCTION --

      static void _restartConnection() {
        ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
          //__whenLineAnyThen(_SM_NOP);
          __clearSF(_SF_IS_CONNECTED);
          __clearSF(_SF_IS_ERROR);
          //delay(1); // no delays needed, line transitions smoothly because of high resistances involved, and no jitterring because of hysteresis in circuits
          __whenLineMThen(_SM_CRIT__WAITING_FOR_CONNECTION);
          _resumeCommunication();
        }
      }

    public:

      static void setup() {
        _lineTransitions._u = _SM_OTH__ERROR_INPUT_COMBINATION;
        _initializePorts();

        _restartConnection();
        //delay(1);
        
        attachInterrupt(digitalPinToInterrupt(PORTD_PIN), _interruptHandler, CHANGE);
        attachInterrupt(digitalPinToInterrupt(PORTD_PIN_CMP), _interruptHandler, CHANGE);
      }

    private:

      /*static volatile bool __justTransitioned = false;
      
      __attribute__((always_inline))
      static void inline _interruptHandlerWithIntermidiateTransitionDetection() {
        if (__justTransitioned) {
          // start timer 1 and leave
        }
      }*/

      __attribute__((always_inline))
      static void inline _interruptHandler() {
        // clear WDT to prevent it from restarting connection
        __clearSF(_SF_WDT);
        LineState ls = _readLine();
        uint8_t state = _lineTransitionsAsArr[(uint8_t)ls];
        if (state == _SM_NOP) {
          // FIXME For interrupt way of handling things - this will never happen! Remove this state - or make it an error (cause it should not happen)
        } else {
          uint8_t group = state & 0xF0;
          if (group == _G_CRIT) {
            _handleCritStates(state);
          } else if (group == _G_HS) {
            _handleHandshakeStates(state);
          } else if (group == _G_RD) {
            _handleReadStates(state, ls);
          } else if (group == _G_WR) {
            _handleWriteStates(state, ls);
          } else if (group == _G_OTH) {
            // FIXME Implement it!
          } else {
            __whenLineAnyThen(_SM_OTH__ALG_ERROR_UNHANDLED_STATE);
          }
        }
      }

      __attribute__((always_inline))
      static void inline _handleCritStates(uint8_t state) {
        // FIXME Order IFs
        if (state == _SM_CRIT__WAITING_FOR_CONNECTION) {                        // Line is M
          // Tx is connected
          __setSF(_SF_IS_CONNECTED);
          __whenLineLThen(_SM_HANDSHAKE__S0);                                   // => waiting for L   (H will produce error)
          
        } else if (state == _SM_CRIT__COMMERROR) {                              // Line is <ANY>
          __setSF(_SF_IS_ERROR);
          __whenLineAnyThen(_SM_CRIT__COMMERROR);                               // => None of states will do any action till timer expires
          _suspendCommunication();                                              // ! Show Tx that we gave-up, now we pull line Up instead!
          __clearSF(_SF_IS_CONNECTED);
          
          // Enable CommError timer, after it elapses - restart communication
          _commErrorTimer = ClockLR::tick();
          __setSF(_SF_COMMERROR_TIMER_ACTIVE);

        } else {
          __whenLineAnyThen(_SM_OTH__ALG_ERROR_UNHANDLED_STATE);
        }
      }

      __attribute__((always_inline))
      static void inline _handleHandshakeStates(uint8_t state) {
        if (state == _SM_HANDSHAKE__S0) {                                       // Line is L
          __whenLineMThen(_SM_HANDSHAKE__S1);                                   // => waiting for M  (H will produce error)
          
        } else if (state == _SM_HANDSHAKE__S1) {                                // Line is M
          __whenLineHThen(_SM_HANDSHAKE__S2);                                   // => waiting for H  (L will produce error)
          
        } else if (state == _SM_HANDSHAKE__S2) {                                // Line is H
          __whenLineMThen(_SM_HANDSHAKE__S3);                                   // => waiting for M  (L will produce error)
          
        } else if (state == _SM_HANDSHAKE__S3) {                                // Line is M
          // Handshake complete, now prepare everything for Reading!
          _initializeRead();
          __whenLineLMHThen(_SM_READ__S0, _SM_CRIT__COMMERROR, _SM_READ__S0);   // => waiting for L or H

        } else {
          __whenLineAnyThen(_SM_OTH__ALG_ERROR_UNHANDLED_STATE);
        }
      }

      static void inline _initializeRead() {
        for (int i = 0; i < SIZE; i++) {
          _readingDataBuf[i] = 0;
        }
        _readingDataBit = 0;
      }

      __attribute__((always_inline))
      static void inline _handleReadStates(uint8_t state, LineState ls) {
        if (state == _SM_READ__S0) {                                            // Line is L or H
          if (ls == L) {                                                        //  Line is L
            __whenLineMThen(_SM_READ__S1);                                      // => waiting for M  (H will produce error)
          } else {                                                              //  Line is H
            _readingDataBuf[_readingDataBit>>3] |= 1 << (_readingDataBit&7);
            __whenLineMThen(_SM_READ__S1);                                      // => waiting for M  (L will produce error)
          }
          
        } else if (state == _SM_READ__S1) {                                     // Line is M
          _readingDataBit++;
          if (_readingDataBit < (SIZE<<3)) { // FIXME - we should also read CRC
            // keep reading
            __whenLineLMHThen(_SM_READ__S0, _SM_CRIT__COMMERROR, _SM_READ__S0); // => waiting for L or H
          } else {
            ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
              for (int i = 0; i < SIZE; i++) {
                _receivedData[i] = _readingDataBuf[i];
              }
              __setSF(_SF_FRESH_DATA);
            }

            // Done Reading
            // Switch to Writing
            _initializeWrite();
            __whenLineHThen(_SM_WRITE_S0);                                      // => waiting for H  (L will produce error)
          }

        } else {
          __whenLineAnyThen(_SM_OTH__ALG_ERROR_UNHANDLED_STATE);
        }
      }

      static void inline _initializeWrite() {
        _writingDataBuf[0] = _writingDataBuf[1] = 0;
        _readingDataBit = 0;
      }

      __attribute__((always_inline))
      static void inline _handleWriteStates(uint8_t state, LineState ls) {
        /*if (state == _SM_WRITE__S0) {                                           // Line is H
          STOP HERE - We cannot directly go from H to L - line will cross M and ISR triggers!!!!
          1) TX: sending 1 system byte first (here we read it)
            it consists of 5 bit for size of data
            2 bits to show last transmission errors (one bit for last two transmissions)
            1 bit shows if last received byte from RX was processed by user sketch (on TX side)
          2) RX: sending 1 user sketch command byte (uscbyte+CRC(includes uscbyte))
          
          if (ls == L) {                                                        //    is L
            __whenLineMThen(_SM_READ__S1);                                      // => waiting for M  (H will produce error)
          } else {                                                              //    is H
            _readingDataBuf[_readingDataBit>>3] |= 1 << (_readingDataBit&7);
            __whenLineMThen(_SM_READ__S1);                                      // => waiting for M  (L will produce error)
          }
          
        } else if (state == _SM_READ__S1) {                                     // Line is M
          _readingDataBit++;
          if (_readingDataBit < (SIZE<<3)) {
            // keep reading
            __lineExpectsLMH(_SM_READ__S0, _SM_CRIT__COMMERROR, _SM_READ__S0);  // => waiting for L or H
          } else {
            ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
              for (int i = 0; i < SIZE; i++) {
                _receivedData[i] = _readingDataBuf[i];
              }
              __setSF(_SF_FRESH_DATA);
            }

            // Done Reading
            // Switch to Writing
            // FIXME Implement this!!!
            //_state = WRITING;
            //__whenLineMThen(_SM_WR_START);
          }

        } else {
          __whenLineAnyThen(_SM_OTH__ALG_ERROR_UNHANDLED_STATE);
        }*/
      }
      
    public:

      // Assumes ClockLR was updated! // FIXME We need this assertion happen at compile time!
      static void tick() {
        if (__isSFSet(_SF_COMMERROR_TIMER_ACTIVE)
              && ClockLR::isElapsed(_commErrorTimer, ERROR_COMMUNICATION_TIMEOUT_MS)) {
                
          __clearSF(_SF_COMMERROR_TIMER_ACTIVE);
          _restartConnection();
        }

        // WDT
        if (!isConnected()) __clearSF(_SF_WDT);
        if (!__isSFSet(_SF_WDT)) {
          // wdt flag was cleared - we can reset timer
          __setSF(_SF_WDT);
          _wdtTimer = ClockLR::now;
        }
        if (ClockLR::isElapsed(_wdtTimer, COMMUNICATION_STALL_TIMEOUT_MS)) {
          __clearSF(_SF_WDT);
          _restartConnection();
        }
      }

      static bool isConnected() {
        return __isSFSet(_SF_IS_CONNECTED);
      }

      static bool isError() {
        return __isSFSet(_SF_IS_ERROR);
      }

      static bool isFreshData() {
        return __isSFSet(_SF_FRESH_DATA);
      }

      static uint8_t getData(uint8_t i) {
        uint8_t data;
        ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
          __clearSF(_SF_FRESH_DATA);
          data = i < SIZE ? _receivedData[i] : 0;
        }
        return data;
      }
      
  };

  template <uint8_t P, uint8_t C, uint8_t PD, uint8_t S>
  volatile uint8_t OneWireErrorReceiver<P,C,PD,S>::_statusFlags = 0;
  template <uint8_t P, uint8_t C, uint8_t PD, uint8_t S>
  volatile uint8_t OneWireErrorReceiver<P,C,PD,S>::_receivedData[S + OneWireErrorReceiver<P,C,PD,S>::_SYS_BYTES] = {};
  template <uint8_t P, uint8_t C, uint8_t PD, uint8_t S>
  uint8_t OneWireErrorReceiver<P,C,PD,S>::_readingDataBuf[S + OneWireErrorReceiver<P,C,PD,S>::_SYS_BYTES] = {};
  template <uint8_t P, uint8_t C, uint8_t PD, uint8_t S>
  uint8_t OneWireErrorReceiver<P,C,PD,S>::_readingDataBit = 0;
  template <uint8_t P, uint8_t C, uint8_t PD, uint8_t S>
  uint8_t OneWireErrorReceiver<P,C,PD,S>::_writingDataBuf[2] = {};
  template <uint8_t P, uint8_t C, uint8_t PD, uint8_t S>
  typename OneWireErrorReceiver<P,C,PD,S>::LineThenState OneWireErrorReceiver<P,C,PD,S>::_lineTransitions;
  template <uint8_t P, uint8_t C, uint8_t PD, uint8_t S>
  uint16_t OneWireErrorReceiver<P,C,PD,S>::_commErrorTimer = 0;
  template <uint8_t P, uint8_t C, uint8_t PD, uint8_t S>
  uint16_t OneWireErrorReceiver<P,C,PD,S>::_wdtTimer = 0;
  template <uint8_t P, uint8_t C, uint8_t PD, uint8_t S>
  uint8_t* const OneWireErrorReceiver<P,C,PD,S>::_lineTransitionsAsArr = 
                    (uint8_t*)&OneWireErrorReceiver<P,C,PD,S>::_lineTransitions;

}

namespace pvt {
  
  template <uint8_t PD, uint8_t SIZE>
  using ErrorReceiver = pvt::toolkit::debug::rx::OneWireErrorReceiver<2, 3, PD, SIZE>;
}

#endif
