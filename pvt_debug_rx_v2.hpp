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

#ifndef PVT_TOOLKIT_DEBUG_RX_V2_H
#define PVT_TOOLKIT_DEBUG_RX_V2_H

#include <Arduino.h>
#include <util/atomic.h>
#include <clock.h>
#include <pvt_crc8.hpp>

namespace pvt::toolkit::debug::rx::v2 {
  
  template<uint8_t PORTD_LINE_PN, uint8_t PORTD_PULL_PN>
  class OneWireErrorReceiver final {
    private:

      static_assert((PORTD_LINE_PN <= 7), "PORTD_LINE_PN must be within 0..7 range!");
      static_assert((PORTD_PULL_PN <= 7), "PORTD_PULL_PN must be within 0..7 range!");
      
      OneWireErrorReceiver() {};
    
      static constexpr uint8_t _LINE_MASK = 1<<PORTD_LINE_PN;
      static constexpr uint8_t _PULL_MASK = 1<<PORTD_PULL_PN;

      static constexpr uint16_t COMM_TIMEOUT_TMR1_OVERFLOWS = 5; // ~20s (each overflow is around 4s)

      static void _init() {
        // set LINE as input - no pull-up
        DDRD &= ~_LINE_MASK;
        PORTD &= ~_LINE_MASK;

        // set PULL as output - pull-up enabled
        // we set internal pull-up first - to prevent line to go down (that can happen if we swap below two instructions)
        PORTD |= _PULL_MASK;
        DDRD |= _PULL_MASK;
      }
      
      static void _setupTimer1() {
        TCCR1A = 0;
        TCCR1B = 0;
        TCCR1B = (1 << CS12) | (1 << CS10); // prescaler = 1024 = 64us
        TCNT1 = 0;
        TIFR1 = (1 << TOV1); // clear Tmr1 Overflow flag
      }
      
      inline static void _pullUp() {
        PORTD |= _PULL_MASK;
      }
      
      inline static void _pullDown() {
        PORTD &= ~_PULL_MASK;
      }

      inline static bool _isPullUpEnabled() {
        return PORTD & _PULL_MASK;
      }
      inline static bool _isPullDownEnabled() {
        return !_isPullUpEnabled();
      }
      
      inline static bool _isLineHigh() {
        return PIND & _LINE_MASK;
      }
      inline static bool _isLineLow() {
        return !_isLineHigh();
      }
      
      // pulledUp=false when actively driven HIGH by TX
      // pulledUp=true when Pulled Up by TX
      // return false if timedout and line is still LOW
      inline static bool _waitForLineToGetHigh(bool &pulledUp) {
        pulledUp = false;
        // Waiting for line to get High ;)
        //uint8_t t1=0; // timeout option 1
        //uint8_t start=TCNT1L; // timeout option 2
        uint8_t overflows=0; // timeout option 3
        TIFR1 = (1 << TOV1); // clear Tmr1 Overflow flag
        while (_isLineLow()) {
          // timeout option 1: (not working due to no interrupts)
          //if (TIFR0 & (1 << TOV0)) {
          //  TIFR0 = (1 << TOV0);
          //  t1++;
          //  if (t1 == 0) { // 256*1.024ms = ~262ms
          //    // error - timed-out waiting for TX
          //    return false;
          //  }
          //}
          
          // timeout option 2: Timer 1: too short
          //if (TCNT1L - start > 16) { // every Timer1 tick is 64uS, here we wait for around 1024uS
          //  // error - timed-out waiting for TX
          //  return false;
          //}
          
          // timeout option 3: Timer 1 overflows
          if (TIFR1 & (1 << TOV1)) {
            TIFR1 = (1 << TOV1); // clear Tmr1 Overflow flag
            overflows++;
            if (overflows > COMM_TIMEOUT_TMR1_OVERFLOWS) {
              // timed out
              return false;
            }
          }
        }

        // Check if TX is pulling line Up or Actively drives it High
        _pullDown();
        __builtin_avr_delay_cycles(2);
        if (_isLineLow()) { // TX is driving line via internal PullUp
          pulledUp = true;
        }
        return true;
      }

      // pulledUp=false when actively driven LOW by TX
      // pulledUp=true when Pulled Up by TX
      // return false if timedout and line is still HIGH
      inline static bool _waitForLineToGetLow(bool &pulledUp) {
        pulledUp = false;
        // Waiting for line to get Low ;)
        uint8_t overflows=0;
        TIFR1 = (1 << TOV1); // clear Tmr1 Overflow flag
        while (_isLineHigh()) {
          if (TIFR1 & (1 << TOV1)) {
            TIFR1 = (1 << TOV1); // clear Tmr1 Overflow flag
            overflows++;
            if (overflows > COMM_TIMEOUT_TMR1_OVERFLOWS) {
              // timed out
              return false;
            }
          }
        }

        // Check if TX is pulling line Up or Actively drives it Low
        _pullUp();
        __builtin_avr_delay_cycles(2);
        if (_isLineHigh()) { // TX is driving line via internal PullUp
          pulledUp = true;
        }
        return true;
      }

      static constexpr uint16_t _PACKET_HANDSHAKE = 5; // HLHLHU
      static constexpr uint16_t _PACKET_1         = 1; // HU
      static constexpr uint16_t _PACKET_0         = 2; // HLU
      static constexpr uint16_t _PACKET_READ_REQ  = 3; // HLHU
      static constexpr uint16_t _PACKET_FRAME_OVER= 1; // HU

      /**
       * returns one of packets:
       *   0 - no packet received or error (check error for details)
       *   5: HLHLHU - Handshake
       *   1: HU     - 1
       *   2: HLU    - 0
       *   3: HLHU   - Read Request
       * 
       * error:
       *   0 - no error
       *   0x1 - Line is expected to be Low
       *   0x2 - time out
       *   0x3 - too long packet sequence
       *
       *   0xE - algorithm error : pull down was not enabled!
       *   0xF - unknown error
       */
      static constexpr uint8_t _READ_PACKET_ERR_TMOUT = 0x2;
      static inline uint8_t _readPacket(uint8_t &error) {
        error = 0xF;

        if (!_isPullDownEnabled()) {
          // Algorithm error : expected to have Pull-Down enabled
          error = 0xE; return 0;
        }
        if (!_isLineLow()) {
          // Line is expected to be Low yet
          error = 0x1; return 0;
        }
        uint8_t res = 0;

        for (uint8_t i = 0; i < 10; i++) {
          
          bool isPU = false;
          if (_waitForLineToGetHigh(isPU)) {
            if (isPU) {
              // End of Packet
              error = 0; return res;
            }
          } else {
            // Timedout
            error = _READ_PACKET_ERR_TMOUT; return res;
          }
          
          res++;

          if (_waitForLineToGetLow(isPU)) {
            if (isPU) {
              // End of Packet
              error = 0; return res;
            }
          } else {
            // Timedout
            error = _READ_PACKET_ERR_TMOUT; return res;
          }
          
          res++;
          
        }
        
        error = 0x3;
        return res;
      }

      /**
       * error:
       *   0x1x - readPacket error; x - at which bit error happened
       *   0x2x - packet is nor 0 nor 1; x - at which bit error happened
       */
      static inline uint8_t _readByte(uint8_t &error) {
        error = 0xFF;
        uint8_t res = 0;
        for (uint8_t bitNo = 0; bitNo < 8; bitNo++) {
          _pullDown(); // make sure pull-down is active
          __builtin_avr_delay_cycles(2);
          uint8_t err = 0;
          uint8_t p = _readPacket(err);
          if (err != 0) {
            error = 0x00 | (bitNo<<4) | err; return res;
          }
          res >>= 1;
          if (p == _PACKET_1) {
            res |= 0x80;
          } else if (p == _PACKET_0) {
          } else {
            error = 0x80 | (bitNo<<4) | err; return res;
          }
        }
        error = 0; return res;
      }

      static bool _writeByte(uint8_t d, uint8_t &error) {
        error = 0xFF;
        uint8_t err = 0;
        
        for (uint8_t bitNo = 0; bitNo < 8; bitNo++) {
          _pullDown(); // make sure pull-down is active
          __builtin_avr_delay_cycles(2);
          uint8_t p = _readPacket(err);
          if (err != 0) {
            error = 0x10 | bitNo; return false;
          }
          if (p != _PACKET_READ_REQ) {
            error = 0x20 | bitNo; return false;
          }
          if (d & 1) {
            _pullUp();
            // FIXME wait for LU confirmation
          } else {
            _pullDown();
            // FIXME wait for HU confirmation
          }
          d>>=1;
        }
        
        error = 0; return true;
      }

      // errors:
      //    0 - no error
      //    0x1x - timed-out in the middle of handshake
      //    0x2x - wrong packet (not handshake)
      //    0x3x - reading packet error
      //    0xFF - unhandled error
      static bool _waitForHandshake(uint8_t &error) {
        error = 0xFF; // Unhandled error
        
        uint8_t err = 0;
        uint8_t res = _readPacket(err);
        if (err == 0 && res == _PACKET_HANDSHAKE) {
          error = 0; return true;

        } else if (err == _READ_PACKET_ERR_TMOUT) {
          if (res == 0) {
            // timed-out at first transition - no TX activity yet
            error = 0; return false;
          } else {
            // timed-out in the middle of handshake - error
            error = 0x10 | res; return false;
          }

        } else {
          // error reading packet
          if (err == 0) {
            error = 0x20 | res;
          } else {
            error = 0x30 | err;
          }
          return false;
        }
      }

      static bool _isReceivedData;
      static uint8_t _receivedDataLength;
      static bool _isPrevTransmissionFailed;
      static bool _isPrevPrevTransmissionFailed;
      static uint8_t _receivedData[34]; // sys byte + data[1~32] + crc

      static bool __readFrame(uint8_t cmd, uint8_t &error, uint8_t &lastReadByteError) {
        error = 0xFF; // Unhandled error
        lastReadByteError = 0xFE; // Unhandled error
        
        _isReceivedData = false;
        for (uint8_t i = 0; i < 34; i++) {
          _receivedData[i] = 0;
        }
        
        _setupTimer1();
          
        ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
          
          if (_isPullDownEnabled()) {
            // Algorithm error!
            error = 0x11; return false;
          }
          if (_isLineLow()) {
            // Line is LOW - expected to be HIGH
            error = 0x12; return false;
          }
          _pullDown(); // starting by pulling line down - to indicate TX that RX is ready
          __builtin_avr_delay_cycles(2);
          
          uint8_t err = 0;
          if (!_waitForHandshake(err)) {
            _pullUp(); // FIXME Consider scenario: What if TX wakes-up right before we pull up? Likelihood is extremely low
            error = err; return false; // if err == 0 => just timed-out; we can restart it later again
          }
          
          // Read Sys Byte
          _receivedData[0] = _readByte(lastReadByteError);
          if (lastReadByteError != 0) {
            error = 0x20; return false;
          }

          _receivedDataLength = (_receivedData[0] & 0x1F) + 1; // 0 means 1 byte, 1 -> 2, ... 31 -> 32
          _isPrevTransmissionFailed = (_receivedData[0] & 0x40);
          _isPrevPrevTransmissionFailed = (_receivedData[0] & 0x20);
          
          // Read Data + CRC
          for (uint8_t i = 1; i < _receivedDataLength + 2; i++) {
            _receivedData[i] = _readByte(lastReadByteError);
            if (lastReadByteError != 0) {
              error = 0x40|i; return false;
            }
          }
          
          // Read Enf Of Transmission (HU)
          _pullDown(); // make sure pull-down is active
          __builtin_avr_delay_cycles(2);
          uint8_t p = _readPacket(err);
          if (err != 0) {
            error = 0x51; return false;
          }
          if (p != _PACKET_FRAME_OVER) {
            error = 0x52; return false;
          }
          
          _pullUp(); // suspend transmission (checking crc before continuing writing)
          
          uint8_t crc = pvt::CRC8::calculate(_receivedData, _receivedDataLength + 1);
          if (crc != _receivedData[_receivedDataLength + 1]) {
            error = 0x70; return false;
          }

          // FIXME Implement change in protocol: TX should hold and wait for RX to 
          //          get ready to write data (calculate CRC takes longer than 1uS expected)

          // Write Cmd Byte
          //bool res = _writeByte(cmd, e);
          //if (!res || e != 0) {
          //  return false;
          //}
          
          _pullUp(); // end transmission
        }
        
        _isReceivedData = true;
        error = 0; return true;
      }
      
    public:

      static void setup() {
        _init();
        _isReceivedData = false;
      }
      
      static bool readFrame(uint8_t cmd, uint8_t &error, uint8_t &lastReadByteError) {
        bool r = __readFrame(cmd, error, lastReadByteError);
        if (true/*!r*/) {
          _pullUp();
        }
        return r;
      }

      static bool isReceivedData() {
        return _isReceivedData;
      }

      static uint8_t getReceivedLength() {
        return _receivedDataLength;
      }

      static uint8_t getDataByte(uint8_t i) {
        if (i < _receivedDataLength) {
          return _receivedData[i + 1];
        } else {
          return 0;
        }
      }
  };

  template <uint8_t L, uint8_t P>
  bool OneWireErrorReceiver<L,P>::_isReceivedData = false;
  template <uint8_t L, uint8_t P>
  uint8_t OneWireErrorReceiver<L,P>::_receivedDataLength = 0;
  template <uint8_t L, uint8_t P>
  bool OneWireErrorReceiver<L,P>::_isPrevTransmissionFailed = false;
  template <uint8_t L, uint8_t P>
  bool OneWireErrorReceiver<L,P>::_isPrevPrevTransmissionFailed = false;

  template <uint8_t L, uint8_t P>
  uint8_t OneWireErrorReceiver<L,P>::_receivedData[34] = {};
  
}

namespace pvt {
  
  template<uint8_t PORTD_LINE_PN, uint8_t PORTD_PULL_PN>
  using ErrorReceiver = pvt::toolkit::debug::rx::v2::OneWireErrorReceiver<PORTD_LINE_PN, PORTD_PULL_PN>;
  
}

#endif
