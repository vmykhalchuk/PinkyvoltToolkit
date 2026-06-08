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

#ifndef PVT_TOOLKIT_DEBUG_TX_V2_H
#define PVT_TOOLKIT_DEBUG_TX_V2_H

#include <Arduino.h>
#include <util/atomic.h>
#include <clock.h>
#include <pvt_crc8.hpp>

namespace pvt::toolkit::debug::tx::v2 {

/**
 * The main idea behind this protocol is that its' FSM will run on user sketch - it should tolerate prolonged periods between tick() calls.
 * Receiver is expecting this and guarantees to responde within 2.5uS window.
 * 
 * Transmitter utilizes eventually-progressing asynchronous transactional bus to communicate data to Receiver.
 */

/*
 * FIXME/TODO List
 *  
 *  - [HIGH][PERFORMANCE] Replace Switch with method pointer to improve perfromance (especially important for wait loops) (best will be simple IJMP (also allign commands into same block sizes, so quick multiplication will lead to required case) to save on RET)
 *  
 *  - [HIGH] Document:
                - line states
                - ownership
                - timing
                - bit encoding
                - startup handshake
                - collision rules
                - timeout behavior
                - recovery behavior
 *  
 *  - Fix registerError function - see its comments
 *  - make it more flexible in configuration (not only PORTD as a comm pin)
 *  - add option to disable extra protocol checks - to save programm space
 *  
 *  
 *  - TODO Consider using table mappings:
 *    - define
 *        using Handler = void(*)();
 *        static constexpr Handler table[];
 *    - use
 *        table[state]();
 *  
 */

  // Communication Errors
  constexpr uint8_t CMERR__OK               = 0x00;
  constexpr uint8_t CMERR__BAD_CRC          = 0x01; // Invalid CRC
  constexpr uint8_t CMERR__PROTOCOL_FAILURE = 0x02; // Protocol failure
  constexpr uint8_t CMERR__STALL            = 0x02; // Communication Stalled
  constexpr uint8_t CMERR__ALG_TXOUT        = 0x10; // Algorithm Error When Transiting TX Out
  constexpr uint8_t CMERR__ALG_BAD_STATE    = 0x20; // Algorithm Error: Unexpected or unregistered state!
  

                             //   A A P _
                             //  _H_L_U_Z
  constexpr uint8_t __Z_to__ = 0B00110100;
  constexpr uint8_t _PU_to__ = 0B11000001;
  constexpr uint8_t _AL_to__ = 0B01000010;
  constexpr uint8_t _AH_to__ = 0B00011000;
                      //=> 2bit opCodes:
                      //        00->not allowed(error)    01->toggle PIN
                      //        10->set as input          11->set as output
  constexpr uint8_t _TRANSITION_MAP[] = {__Z_to__, _PU_to__, _AL_to__, _AH_to__};

  template <uint8_t PORTD_PIN, uint8_t SIZE, bool STALL_PREVENTION, bool DEBUG_ENABLED>
  class OneWireErrorTransmitter final {
    
    private:
      static constexpr bool __debug = DEBUG_ENABLED;

      static constexpr uint8_t _SYS_BYTES = 2; // FIXME Implement this! (See Receiver for details)

      static_assert(SIZE >=1 && SIZE <= 29, "SIZE must be in range [1..29]");
      static_assert(((SIZE + _SYS_BYTES)<<3) <= 0xFF, "SIZE+sysBytes must be addressable with single byte");

      static_assert((PORTD_PIN <= 7), "PORTD_PIN must be within 0..7 range!");

      OneWireErrorTransmitter() {};

      static constexpr uint8_t _MASK = 1<<PORTD_PIN;

      enum FSMStateGroup : uint8_t {
        _G_CRIT = 0x10, // Critical and oftenly active
        _G_HS   = 0x20, // Handshake
        _G_WR   = 0x30, // Writing
        _G_RD   = 0x40, // Reading
        _G_NC   = 0xC0  // Non Critical
      };
      
      enum FSMState : uint8_t {
        // -- Special cases (Handled first) --
        // Assures Receiver has min 4uS time to react. Note: It can be way above that time - all depends on how often tick() is called
        _SPEC__SKIP_FULL_CYCLE              = 0 | 0,
        _SPEC__WAITING_FOR_HANDSHAKE        = 0 | 1,

        // -- CRITICAL --
      //_CRIT__SKIP_FULL_CYCLE              = _G_CRIT | 0, // Moved to Special case
      //_CRIT__WAITING_FOR_HANDSHAKE        = _G_CRIT | 0,
        _CRIT__COMMERROR                    = _G_CRIT | 1,
        _CRIT__LONG_WAIT                    = _G_CRIT | 2,
        _CRIT__ALG_ERROR_TXOUT              = _G_CRIT | 0xC, // Algorithimc error - TX Out
        _CRIT__ALG_ERR__BAD_STATE           = _G_CRIT | 0xD, // Algorithimc error - Unexpected or Not registered state

        // -- HANDSHAKE --
        _HS__S1                             = _G_HS | 1,
        _HS__S2                             = _G_HS | 2,
        _HS__S3                             = _G_HS | 3,
        _HS__S4                             = _G_HS | 4,
        _HS__S5                             = _G_HS | 5,
        _HS__S6                             = _G_HS | 6,

        // -- WRITING --
        _WR_START                           = _G_WR | 0,
        _WR_S0                              = _G_WR | 1,
        _WR_S1                              = _G_WR | 2,
        _WR_S2                              = _G_WR | 3,
        _WR_S3                              = _G_WR | 4,
        _WR_S4                              = _G_WR | 5,
        _WR_S5                              = _G_WR | 6,
      //_WR_ERROR                           = _G_WR | 7,

        // -- READING --
        _RD_START                           = _G_RD | 0,
        _RD_S0                              = _G_RD | 1,
        _RD_S1                              = _G_RD | 2,
        _RD_S2                              = _G_RD | 3,
        _RD_S3_L                            = _G_RD | 4,
        _RD_S4_H                            = _G_RD | 5,
        _RD_S5                              = _G_RD | 6,
        _RD_ERROR                           = _G_RD | 7,
        
        _NC_SETUP                           = _G_NC | 0,

        _NOOP                               = 0xFF
      };


      static uint8_t _skipCycleTmr0;
      static uint8_t _skipCycleTmr0p1;

      static FSMState _if_L_then;
      static FSMState _if_H_then;
      
      static FSMState _if_L_then__saved4waitFullCycle;
      
      inline static void _waitFullCycleAndSwitchToLH(FSMState if_L_then, FSMState if_H_then) {
        if (__debug) {
          Serial.print("WtLH:"); Serial.print(if_L_then, HEX); Serial.print(':'); Serial.println(if_H_then,HEX);
          delay(50);
        }
        
        _if_L_then = _SPEC__SKIP_FULL_CYCLE;
        _if_L_then__saved4waitFullCycle = if_L_then;
        _if_H_then = if_H_then;
        
        asm volatile("" ::: "memory");
        _skipCycleTmr0p1 = _skipCycleTmr0 = TCNT0;
        _skipCycleTmr0p1++;
      }
      
      inline static void _switchToLH(FSMState if_L_then, FSMState if_H_then) {
        if (__debug) {
          Serial.print("SwLH:"); Serial.print(if_L_then, HEX); Serial.print(':'); Serial.println(if_H_then,HEX);
          delay(300);
        }
        _if_L_then = if_L_then;
        _if_H_then = if_H_then;
      }

      // Set D<PORTD_PIN> To Output (1 cycle)
      inline static void _setDPinToOutput() { asm volatile ("sbi %0, %1" : : "I" (_SFR_IO_ADDR(DDRD)), "I" (PORTD_PIN) : "memory"); }

      // Set D<PORTD_PIN> To Output (1 cycle)
      inline static void _setDPinToInput() { asm volatile ("cbi %0, %1" : : "I" (_SFR_IO_ADDR(DDRD)), "I" (PORTD_PIN) : "memory"); }

      // Set D<PORTD_PIN> HIGH (1 cycle) 
      inline static void _setDPinToHigh() { asm volatile ("sbi %0, %1" : : "I" (_SFR_IO_ADDR(PORTD)), "I" (PORTD_PIN) : "memory"); }

      // Set D<PORTD_PIN> LOW (1 cycle)
      inline static void _setDPinToLow() { asm volatile ("cbi %0, %1" : : "I" (_SFR_IO_ADDR(PORTD)), "I" (PORTD_PIN) : "memory"); }

      // Toggle D<PORTD_PIN> (1 cycle)
      // Writing a 1 to the PIN register toggles the PORT bit
      inline static void _toggleDPinState() { asm volatile ("sbi %0, %1" : : "I" (_SFR_IO_ADDR(PIND)), "I" (PORTD_PIN) : "memory"); }

      inline static bool _isDPinHigh() { return PIND & _MASK; }

      inline static bool _isDPinLow() { return !(PIND & _MASK); }

      enum TXMode : uint8_t {
        __Z, //00
        _PU, //01
        _AL, //10
        _AH, //11
        TXModeCount
      };
      
      /*TXMode _currTXMode = __Z;

      __attribute__((always_inline))
      inline void transitionTX(TXMode changeTo) {
        static_assert(TXModeCount==4, "TXModeCount must be = 4!");

        uint8_t _trnst = _TRANSITION_MAP[(uint8_t)_currTXMode]; //TODO [PERFORMANCE] 
        uint8_t opCode = _trnst >> ((uint8_t)changeTo<<1);      //TODO [PERFORMANCE] replace with direct map access - to remove bit manipulations which are slow (make it tunable via constexpr bool, so user can decide speed vs code compactness)
        opCode &= 0x3;

        switch (opCode) {
          case 0: // not allowed (error)
            _switchToLH(_CRIT__ALG_ERROR_TXOUT,_CRIT__ALG_ERROR_TXOUT);
            break;
          case 1: // toggle pin
            _toggleDPinState();
            break;
          case 2: // set as input
            _setDPinToInput();
            break;
          case 3: // set as output
            _setDPinToOutput();
            break;
        }

        _currTXMode = changeTo;
      }*/

      inline static TXMode _getTx() {
        uint8_t dir = ((DDRD >> PORTD_PIN) & 1) << 1;
        uint8_t state = ((PORTD >> PORTD_PIN) & 1);
        return (TXMode)(dir|state);
      }

      inline static void _tx_debug(int code) {
        Serial.print("!!! Wrong initial state: "); Serial.print(_getTx()); Serial.print(". Transition code: "); Serial.println(code, HEX);
        delay(1000);
      }

      inline static void _tx_Z2PU() {
        if (__debug && (_getTx() != __Z)) {
          _tx_debug(1); return;
        }
        _toggleDPinState();
      }
      inline static void _tx_Z2AL() {
        if (__debug && (_getTx() != __Z)) {
          _tx_debug(2); return;
        }
        _setDPinToOutput();
      }
      //inline static void _tx_Z2AH() IMPOSSIBLE With single transition
      
      inline static void _tx_PU2Z() {
        if (__debug && (_getTx() != _PU)) {
          _tx_debug(3); return;
        }
        _toggleDPinState();
      }
      inline static void _tx_PU2AH() {
        if (__debug && (_getTx() != _PU)) {
          _tx_debug(4); return;
        }
        _setDPinToOutput();
      }
      //inline static void _tx_PU2AL() IMPOSSIBLE With single transition

      inline static void _tx_AL2Z() {
        if (__debug && (_getTx() != _AL)) {
          _tx_debug(5); return;
        }
        _setDPinToInput();
      }
      inline static void _tx_AL2AH() {
        if (__debug && (_getTx() != _AL)) {
          _tx_debug(6); return;
        }
        _toggleDPinState();
      }
      //inline static void _tx_AL2PU() IMPOSSIBLE With single transition

      inline static void _tx_AH2PU() {
        if (__debug && (_getTx() != _AH)) {
          _tx_debug(7); return;
        }
        _setDPinToInput();
      }
      inline static void _tx_AH2AL() {
        if (__debug && (_getTx() != _AH)) {
          _tx_debug(8); return;
        }
        _toggleDPinState();
      }
      //inline static void _tx_AH2Z() IMPOSSIBLE With single transition
      

      static volatile uint8_t _data[SIZE];
      static volatile uint8_t _commError;

      static uint8_t _writingData[SIZE + _SYS_BYTES];
      static uint8_t _readingData;
      static uint8_t _bitNo;

      __attribute__((always_inline))
      inline static void __setup() {
        // FIXME Validate if TIMER0 is configured as regular (prescaler=64 @ 16MHz speed && is enabled)
        {
          _setDPinToHigh();
          _setDPinToInput();
          //_currTXMode = _PU;
        }
        _waitFullCycleAndSwitchToLH(_SPEC__WAITING_FOR_HANDSHAKE, _NOOP);
      }
      
    public:

      static void setErrorFlag(uint8_t flagNo) {
        uint8_t dataByteNo = flagNo>>3;
        if (dataByteNo >= SIZE) return;
        // TODO Make this operation atomic
        //    +1) Suspend interrupts
        //     2) ignore it - since we are setting bit - set after set => same result
        //     3) [BEST] make it configurable (one of above three solutions)
        ATOMIC_BLOCK(ATOMIC_RESTORESTATE) { // FIXME use template boolean to either make class thread-safe or not (for advanced users - no need in these additional locks)
                                            // simply implement if (THREAD_SAFE_ENABLED) _setErrorFlagThreadSafe(flagNo) else _setErrorFlag(flagNo);
                                            // _setErrorFlagThreadSafe() utilizes _setErrorFlag() but also has this ATOMIC_BLOCK
                                            // Do that for all methods that access shared variables
          _data[dataByteNo] |= 1 << (flagNo & 0x7);
        }
      }

      static void tick() {
        // Handle most common step first (Skip Full Cycle : meaning wait minimum 4uS)
        if (_if_L_then == _SPEC__SKIP_FULL_CYCLE) {
          // we first check for +1 cycle, we expect in most cases user sketch will be slower and will not call tick() within same 4uS window twice
          if (_skipCycleTmr0p1 != TCNT0) {
            if (_skipCycleTmr0 != TCNT0) {
              _if_L_then = _if_L_then__saved4waitFullCycle;
            } else {
              // we are still at the beginning - wait more (apparently this is test sketch, testing for speed performance)
            }
          }
        } else {
          bool isLow = _isDPinLow();
          // Waiting for Handshake is second most common step : checking it before all else
          // NB: Only _SPEC__WAITING_FOR_HANDSHAKE will not trigger SPL, no need to maintaint SPL here
          if (_if_L_then == _SPEC__WAITING_FOR_HANDSHAKE) {
            // line == L (SPD+PU) (Receiver connected) || line == H (Z+PU) (no Receiver connected)
            if (isLow) {
              // FIXME Add Debouncing here, this LOW might happen shortly and back to HIGH many times due to connection jittering when Physical connection is made
              //        This can be remediated by adding additional pin to Receiver to sense voltage presence from TX, and set delay before starting any attempt to communicate.
              _waitFullCycleAndSwitchToLH(_HS__S1, _CRIT__COMMERROR);
              __resetStallPreventionLogic();
              if (STALL_PREVENTION) {
                _whenStallRevertTo = _CRIT__COMMERROR;
              }
            } else {
              // keep waiting
            }
          } else {
            _handleLineStateChangeWithStallPrevention(isLow);
          }
        }
      }

      static uint8_t getCommunicationError() {
        uint8_t e = _commError;
        _commError = 0;
        return e;
      }
      
      static uint8_t getLastFrameSentNo() { return 0; } // FIXME Implement
      static uint8_t getLastSucceesfulFrameSentNo() { return 0; } // FIXME Implement
      
      // Helping methods to let TX device free line, and freely go to long slumber. First disable communication, than run as many times `tick()` as needed, till isCommunicating() returns false
      static void enableCommunication() {}; // FIXME Implement
      static void disableCommunication() {}; // FIXME Implement
      static bool isCommunicating() { return true; }; // FIXME Implement

    private:

      static FSMState _stallStateIfL, _stallStateIfH;
      static FSMState _whenStallRevertTo;
      static uint8_t _stallCounter;

      inline static void __resetStallPreventionLogic() {
        if (!STALL_PREVENTION) {
          return;
        }
        // FIXME [PERFORMANCE] We might remove these variables, and leave only counter
        //      all we have to do - is add a call to this function from `_switchToLH()` and `_waitFullCycleAndSwitchToLH()`
        _stallStateIfL = _if_L_then;
        _stallStateIfH = _if_H_then;
        _stallCounter = 0;
      }

      __attribute__((always_inline))
      inline static void _handleLineStateChangeWithStallPrevention(bool isLow) {
        if (STALL_PREVENTION) {
          // Stall Prevention Logic
          //    When Receiver fails to respond, FSM will be reverted to `_whenStallRevertTo` state
          if (_stallStateIfL == _if_L_then && _stallStateIfH == _if_H_then) {
            if (++_stallCounter > 20) {
              // Stalled
              _commError |= CMERR__STALL;
              _waitFullCycleAndSwitchToLH(_whenStallRevertTo, _whenStallRevertTo);
            }
          } else {
            __resetStallPreventionLogic();
          }
        }
        
        _handleLineStateChange(isLow);
      }

      __attribute__((always_inline))
      inline static void _handleLineStateChange(bool isLow) {
        FSMState state = isLow ? _if_L_then : _if_H_then;
        uint8_t group = state & 0xF0;

        if (_G_CRIT == group) {
          _handleCritState(state);
        } else if (_G_HS == group) {
          _handleHandshakeState(state);
        } else if (_G_WR == group) {
          _handleWritingState(state);
        } else if (_G_RD == group) {
          _handleReadingState(state);
        } else if (_G_NC == group) {
          _handleNonCritState(state);

        } else {
          _switchToLH(_CRIT__ALG_ERR__BAD_STATE, _CRIT__ALG_ERR__BAD_STATE);
        }
      }

      __attribute__((always_inline))
      inline static void _handleCritState(FSMState state) {
        if (_CRIT__COMMERROR == state) {// Line == <ANY>
          _commError |= CMERR__PROTOCOL_FAILURE;
          // FIXME Add restart here after waiting full cycle two times
          setup();

        } else if (_CRIT__ALG_ERROR_TXOUT == state) {// Line == <ANY>
          _commError |= CMERR__ALG_TXOUT;
          _switchToLH(_CRIT__COMMERROR, _CRIT__COMMERROR);

        } else if (_CRIT__ALG_ERR__BAD_STATE == state) {// Line == <ANY>
          _commError |= CMERR__ALG_BAD_STATE;
          _switchToLH(_CRIT__COMMERROR, _CRIT__COMMERROR);

        } else {
          _switchToLH(_CRIT__ALG_ERR__BAD_STATE, _CRIT__ALG_ERR__BAD_STATE);
        }
      }

      __attribute__((always_inline))
      inline static void _handleHandshakeState(FSMState state) {

        if (_HS__S1 == state) {                 // line == L (SPD+PU)
          _tx_PU2AH();                          // TX=>AH; Line=H (SPD+AH)
          _waitFullCycleAndSwitchToLH(_CRIT__COMMERROR, _HS__S2);
          
        } else if (_HS__S2 == state) {          // line == H (SPD+AH)
          _tx_AH2AL();                          // TX=>AL; Line=L (SPD+AL)
          _waitFullCycleAndSwitchToLH(_HS__S3, _CRIT__COMMERROR);

        } else if (_HS__S3 == state) {          // line == L (SPU+AL)
          _tx_AL2AH();                          // TX=>AH; Line=H (SPU+AH)
          _waitFullCycleAndSwitchToLH(_CRIT__COMMERROR, _HS__S4);
          
        } else if (_HS__S4 == state) {          // line == H (SPU+AH)
          _tx_AH2AL();                          // TX=>AL; Line=L (SPU+AL)
          _waitFullCycleAndSwitchToLH(_HS__S5, _CRIT__COMMERROR);
          
        } else if (_HS__S5 == state) {          // line == L (SPU+AL)
          _tx_AL2AH();                          // TX=>AH; Line=H (SPU+AH)
          _waitFullCycleAndSwitchToLH(_CRIT__COMMERROR, _HS__S6);
          
        } else if (_HS__S6 == state) {          // line == H (SPD+AH)
          _tx_AH2PU();                          // TX=>PU; Line=L (SPD+PU)
          // in this moment RX tests if line is PU or AL, and immediately goes to SPD
          //                   tests by pulling line Up for very short moment
          //                   all must happen within 1.5uS window
          _waitFullCycleAndSwitchToLH(_WR_START, _CRIT__COMMERROR); // TX is forced to wait 4uS minimum, only then check line
          
        } else {
          _switchToLH(_CRIT__ALG_ERR__BAD_STATE, _CRIT__ALG_ERR__BAD_STATE);
        }
      }

      __attribute__((always_inline))
      inline static void _handleWritingState(FSMState state) {

        if (_WR_START == state) {               // Line == L (SPD+PU)
          ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
            // TODO [OPTIMIZATION] Make it tunable over constexpr boolean if we write directly from _data or copy first (also will disable atomicity at the same time)
            //                        at the start of method defineL uint16_t &dRef = _writingData; <-- or dRef = _data - based on constexpr
            //                        and use dRef instead of _writingData
            _preserveWritingData(); // reset - if writing fails we revert _data with _writingData
          }
          _bitNo = 0; // start from bit 0
          if (STALL_PREVENTION) {
            _whenStallRevertTo = _RD_ERROR;
          }
          _switchToLH(_WR_S0, _RD_ERROR);
          
        } else if (_WR_S0 == state) {           // Line == L (SPD+PU)
          _tx_PU2AH();                          // TX=>AH; Line=H (SPD+AH)
          _waitFullCycleAndSwitchToLH(_RD_ERROR, _WR_S1);
          
        } else if (_WR_S1 == state) {           // Line == H (SPD+AH)
          bool isHigh = _writingData[_bitNo >> 3] & (1<<(_bitNo&7));
          if (isHigh) {
            // Write 1
            _tx_AH2PU();                        // TX=>PU; Line=L (SPD+PU)
            _waitFullCycleAndSwitchToLH(_WR_S3, _RD_ERROR);
          } else {
            // Write 0
            _tx_AH2AL();                        // TX=>AL; Line=L (SPD+AL)
            _waitFullCycleAndSwitchToLH(_WR_S2, _RD_ERROR);
          }
          
        } else if (_WR_S2 == state) {           // Line == L (SPU+AL)
          _tx_AL2Z();                           // TX=> Z; Line=H (SPU+ Z) // we do not need to stop interrupts here - even if it happens that code is interrupted and delayed - if all right on Receiver side - it will not harm protocol ... theoretically
                                                                           // the reason it will keep working is:
                                                                           //    Z is same as PU - will let RX verify if line can be strong pulled Down (meaning PU is activated on TX side, thats what Receiver has to know)
          _tx_Z2PU();                           // TX=>PU; Line=H (SPU+PU)
          _waitFullCycleAndSwitchToLH(_WR_S3, _RD_ERROR);
        
        } else if (_WR_S3 == state) {           // Line == L (SPD+PU)
          _bitNo++;
          if (_bitNo < ((SIZE+2)<<3)) { // FIXME We have hard limit here which is lower than 32, bitNo must be 16bit instead!
            _switchToLH(_WR_S0, _RD_ERROR);
          } else {
            // last bit was written - now TX should reply with `HU` to end transmission
            _waitFullCycleAndSwitchToLH(_WR_S4, _RD_ERROR);
          }
          
        } else if (_WR_S4 == state) {           // Line == L (SPD+PU)
          _tx_PU2AH();                          // TX=>AH; Line=H (SPD+AH)
          _waitFullCycleAndSwitchToLH(_RD_ERROR, _WR_S5);
        
        } else if (_WR_S5 == state) {           // Line == H (SPD+AH)
          _tx_AH2PU();                          // TX=>PU; Line=L (SPD+PU)  // Here line becomes L, RX knows that current frame is over,
                                                                            // now it pulls line Up, indicating it is done and busy with its own tasks.
                                                                            // Q: But how would it know when TX is ready to continue?
                                                                            // A: TX should expect either state - H - wait; L - start handshake
        
          // Done writing - switching to Waiting for handshake
          if (STALL_PREVENTION) {
            _whenStallRevertTo = _CRIT__COMMERROR;
          }
          _waitFullCycleAndSwitchToLH(_SPEC__WAITING_FOR_HANDSHAKE, _SPEC__WAITING_FOR_HANDSHAKE);
          
        } else {
          _switchToLH(_CRIT__ALG_ERR__BAD_STATE, _CRIT__ALG_ERR__BAD_STATE);
        }
      }
      
      __attribute__((always_inline))
      inline static void _handleReadingState(FSMState state) {

        if (_RD_START == state) {               // Line == M (PD+PU)
          _readingData = 0;
          _bitNo = 0;
          _switchToLH(_RD_S0, _RD_ERROR);
          
        } else if (_RD_S0 == state) {           // Line == M (PD+PU)
          _tx_PU2AH();                          // TX=>AH; Line=H (PD+AH)
          _waitFullCycleAndSwitchToLH(_RD_ERROR, _RD_S1);

        } else if (_RD_S1 == state) {           // Line == H (PD+AH)
          _tx_AH2AL();                          // TX=>AL; Line=L (PD+AL) (No possible action on Receiver side can change Line state, so it is safe to transition here in two steps)
          _tx_AL2Z();                           // TX=> Z; Line=L (PD+Z)
          _waitFullCycleAndSwitchToLH(_RD_S2, _RD_ERROR);

        } else if (_RD_S2 == state) {           // Line == L (PD+Z)
          _tx_Z2PU();                           // TX=>PU; Line=M (PD+PU)
          _waitFullCycleAndSwitchToLH(_RD_S3_L, _RD_S4_H);
          
        } else if (_RD_S3_L == state) {         // Line == M (PD+PU) // Receiver decided on 0
          _tx_PU2Z();                           // TX=> Z; Line=L (PD+Z) // FIXME Make Sure Receiver DOESN'T ACT AT THIS M=>L TRANSITION!!! (Receiver must wait till line becomes M again)
          _tx_Z2AL();                           // TX=>AL; Line=L (PD+AL)
          _waitFullCycleAndSwitchToLH(_RD_S5, _RD_ERROR);

        } else if (_RD_S4_H == state) {         // Line == H (Z+PU) // Receiver decided on 1
          _readingData |= 1 << _bitNo;
          _tx_PU2AH();                          // TX=>AH; Line=H (Z+AH)
          _tx_AH2AL();                          // TX=>AL; Line=L (Z+AL)
          _waitFullCycleAndSwitchToLH(_RD_S5, _RD_ERROR);
          
        } else if (_RD_S5 == state) {           // Line == L (PD+AL)
          _tx_AL2Z();                           // TX=>Z; Line=L (PD+Z)
          _tx_Z2PU();                           // TX=>PU; Line=M (PD+PU)
          _bitNo++;
          if (_bitNo < 8) { // we read 8 bits (match it with Receiver!)
            _waitFullCycleAndSwitchToLH(_RD_S0, _RD_ERROR);
          } else {
            // last bit was read -> validating -> exiting
            if (_validateData()) {
              if (STALL_PREVENTION) {
                _whenStallRevertTo = _CRIT__COMMERROR;
              }
              _waitFullCycleAndSwitchToLH(_SPEC__WAITING_FOR_HANDSHAKE, _SPEC__WAITING_FOR_HANDSHAKE);
            } else {
              // Invalid CRC data
              _commError |= CMERR__BAD_CRC;
              _switchToLH(_RD_ERROR, _RD_ERROR);
            }
          }
          
        } else if (_RD_ERROR == state) {        // Line == <ANY>
          _recoverDataBack();
          if (STALL_PREVENTION) {
            _whenStallRevertTo = _CRIT__COMMERROR;
          }
          _switchToLH(_CRIT__COMMERROR, _CRIT__COMMERROR);

        } else {
          _switchToLH(_CRIT__ALG_ERR__BAD_STATE, _CRIT__ALG_ERR__BAD_STATE);
        }
        
      }

      __attribute__((always_inline))
      inline static void _handleNonCritState(FSMState state) {
        if (_NC_SETUP == state) {               // Line == Any
          __setup();
          
        } else {
          _switchToLH(_CRIT__ALG_ERR__BAD_STATE, _CRIT__ALG_ERR__BAD_STATE);
        }
        
      }

      inline static bool _validateData() {
        uint8_t crc8 = pvt::CRC8::calculate(_writingData, SIZE + _SYS_BYTES);
        return _readingData == crc8;
      }

      inline static void _preserveWritingData() {
        ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
          for (uint8_t i = 0; i < SIZE; i++) {
            _writingData[i+1] = _data[i];
            _data[i] = 0;
          }
        }
        uint8_t sysByte = (SIZE - 1);
        // FIXME set 0x40 0x20 flags (marking latest failures if present)
        _writingData[0] = sysByte;
        _writingData[SIZE + 1] = CRC8::calculate(_writingData, 1 + SIZE); // SYS Byte + Data
      }
  
      inline static void _recoverDataBack() {
        ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
          for (uint8_t i = 0; i < SIZE; i++) {
            // We recover back with assumption that data is set of flags. All flags that were set before are kept as is
            _data[i] |= _writingData[1+i]; // We skip SYS Byte in _writingData
            // FIXME Consider if above logic is right, maybe make it configurable (if user do not wants it to be flags only)
          }
        }
      }

  };

  template <uint8_t P, uint8_t S, bool _SP, bool _DE>
  uint8_t OneWireErrorTransmitter<P, S, _SP, _DE>::_skipCycleTmr0 = 0;
  template <uint8_t P, uint8_t S, bool _SP, bool _DE>
  uint8_t OneWireErrorTransmitter<P, S, _SP, _DE>::_skipCycleTmr0p1 = 0;

  template <uint8_t P, uint8_t S, bool _SP, bool _DE>
  typename OneWireErrorTransmitter<P, S, _SP, _DE>::FSMState OneWireErrorTransmitter<P, S, _SP, _DE>::_if_L_then
                                                  = OneWireErrorTransmitter<P, S, _SP, _DE>::_NC_SETUP;
  template <uint8_t P, uint8_t S, bool _SP, bool _DE>
  typename OneWireErrorTransmitter<P, S, _SP, _DE>::FSMState OneWireErrorTransmitter<P, S, _SP, _DE>::_if_H_then
                                                  = OneWireErrorTransmitter<P, S, _SP, _DE>::_NC_SETUP;
  template <uint8_t P, uint8_t S, bool _SP, bool _DE>
  typename OneWireErrorTransmitter<P, S, _SP, _DE>::FSMState OneWireErrorTransmitter<P, S, _SP, _DE>::_if_L_then__saved4waitFullCycle
                                                  = OneWireErrorTransmitter<P, S, _SP, _DE>::_NOOP;

  template <uint8_t P, uint8_t S, bool _SP, bool _DE>
  volatile uint8_t OneWireErrorTransmitter<P, S, _SP, _DE>::_data[S] = {};
  template <uint8_t P, uint8_t S, bool _SP, bool _DE>
  volatile uint8_t OneWireErrorTransmitter<P, S, _SP, _DE>::_commError = CMERR__OK;

  template <uint8_t P, uint8_t S, bool _SP, bool _DE>
  uint8_t OneWireErrorTransmitter<P, S, _SP, _DE>::_writingData[S + OneWireErrorTransmitter<P, S, _SP, _DE>::_SYS_BYTES] = {};
  template <uint8_t P, uint8_t S, bool _SP, bool _DE>
  uint8_t OneWireErrorTransmitter<P, S, _SP, _DE>::_readingData = 0;
  template <uint8_t P, uint8_t S, bool _SP, bool _DE>
  uint8_t OneWireErrorTransmitter<P, S, _SP, _DE>::_bitNo = 0;

  template <uint8_t P, uint8_t S, bool _SP, bool _DE>
  typename OneWireErrorTransmitter<P, S, _SP, _DE>::FSMState OneWireErrorTransmitter<P, S, _SP, _DE>::_stallStateIfL
                                                  = OneWireErrorTransmitter<P, S, _SP, _DE>::_NOOP;
  template <uint8_t P, uint8_t S, bool _SP, bool _DE>
  typename OneWireErrorTransmitter<P, S, _SP, _DE>::FSMState OneWireErrorTransmitter<P, S, _SP, _DE>::_stallStateIfH
                                                  = OneWireErrorTransmitter<P, S, _SP, _DE>::_NOOP;
  template <uint8_t P, uint8_t S, bool _SP, bool _DE>
  typename OneWireErrorTransmitter<P, S, _SP, _DE>::FSMState OneWireErrorTransmitter<P, S, _SP, _DE>::_whenStallRevertTo
                                                  = OneWireErrorTransmitter<P, S, _SP, _DE>::_NOOP;
  template <uint8_t P, uint8_t S, bool _SP, bool _DE>
  uint8_t OneWireErrorTransmitter<P, S, _SP, _DE>::_stallCounter = 0;


/*
      // TODO Clean-up below (Preserve skipTwoFullCycles)

      uint8_t _skipCycleTmr0 = 0;
      uint8_t _skipCycleTmr0p1 = 0;
      uint8_t _skipCycleCounter = 0;

      // Assures Receiver has min 8uS time to react (1 TCNT0 increment is 4uS)
      inline void skipTwoFullCycles(SMState retState) {
        _skipCycleReturnState = retState; // save where to return to after wait
        _skipCycleCounter = 0;
        _state = SKIP_TWO_FULL_CYCLES;

        asm volatile("" ::: "memory");
        _skipCycleTmr0 = TCNT0;
      }

      // Assures Receiver has min 8uS time to react (1 TCNT0 increment is 4uS)
      case SKIP_TWO_FULL_CYCLES:
        if (TCNT0 - _skipCycleTmr0 > 2) {
          _state = _skipCycleReturnState; // done waiting
        } else {
          if (++_skipCycleCounter > 20) { // 20 * 10 instructions (less is physically impossible) = 200*62.5nS=12.5uS
            // This is fall-back mechanism
            // With 3 / 256 timer states - the chances are that some rhytmic calls will leave SM in this state for very long
            _state = _skipCycleReturnState; // done waiting
          }
        }
        break;
  
*/


}

namespace pvt {
  
  template<uint8_t PORTD_PIN, uint8_t SIZE>
  using ErrorTransmitter = pvt::toolkit::debug::tx::v2::OneWireErrorTransmitter<PORTD_PIN, SIZE, true, false>;
  
}

#endif
