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
      OneWireErrorReceiver() {};
    
      static constexpr uint8_t _LINE_MASK = 1<<PORTD_LINE_PN;
      static constexpr uint8_t _PULL_MASK = 1<<PORTD_PULL_PN;

      static constexpr uint16_t HANDSHAKE_TIMEOUT_TMR1_OVERFLOWS = 5; // each overflow is around 4s

      static void _init() {
        // set LINE as input - no pull-up
        DDRD &= ~_LINE_MASK;
        PORTD &= ~_LINE_MASK;

        // set PULL as output - pull-up enabled
        PORTD |= _PULL_MASK;
        DDRD |= _PULL_MASK;
      }
      
      static void _setupTimer1() {
        TCCR1A = 0;
        TCCR1B = 0;
        TCCR1B = (1 << CS12) | (1 << CS10); // prescaler = 1024 = 64us
        TCNT1 = 0;
      }
      
      inline static void noop2x() {
        asm volatile ("NOP" : : : "memory");
        asm volatile ("NOP" : : : "memory");
      }

      inline static void _pullUp() {
        PORTD |= _PULL_MASK;
        // Required:
        // allow GPIO/bus voltage to settle before sampling PIND.
        // We intentionally detect contention through 200R coupling.
        __builtin_avr_delay_cycles(2);
        //noop2x();
      }
      inline static void _pullDown() {
        PORTD &= ~_PULL_MASK;
        noop2x();
      }

      inline static bool _isPullUp() {
        return PORTD & _PULL_MASK;
      }
      inline static bool _isPullDown() {
        return !(PORTD & _PULL_MASK);
      }
      
      inline static bool _isLineHigh() {
        return PIND & _LINE_MASK;
      }
      inline static bool _isLineLow() {
        return !(PIND & _LINE_MASK);
      }

      static constexpr uint16_t _PACKET_HANDSHAKE = 0B10101;
      static constexpr uint16_t _PACKET_1         = 0B00001;
      static constexpr uint16_t _PACKET_0         = 0B00010;
      static constexpr uint16_t _PACKET_READ_REQ  = 0B00101;

      /*
          Note: RX Drives line via 200R resistor, making it possible for TX to override line state
          /// Start And Handshake (TX is powered first)
          RXST: 0       1   2 3      4      5 6     7      8 9   a b    c d    
            RX:             r r......r......r r.....r r....r r...r r....r r....
            RX: Z       U    D               U       D      U     D      U
            LN: Z...H........L......H......L.......H......L.....H......L.H.....
            TX: Z   U               H      L       H      L     H      U      
            TX:           r   r    r                                          r
          TXST: 0   1     2   3    4       5       6      7     8      9      a
       */
      /**
       * returns one of packets:
       *   0 - no packet received or error (check error for details)
       *   0B10101 - Handshake
       *   0B1     - 1
       *   0B10    - 0
       *   0B101   - Read Request
       * 
       * error:
       *   0 - no error
       *   0xFF - unknown error
       *   0xEx - algorithm error
       *   0x1x - communication start error
       *   0x5x - timeout error, x - transition at which error happened
       */
      static uint16_t _readPacket(uint8_t &error) {
        error = 0xFF;

        if (!_isPullUp()) {
          // Algorithm error : expected to have Pull-Up enabled
          error = 0xE0;
          return 0;
        }


        ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
          // step 2
          if (_isLineLow()) {
            // error - TX is actively driving line Low (most likely it is in the middle of faulty transmission)
            error = 0x11;
            return 0;
          }
          _pullDown(); // Start packet
          // step 3
          if (_isLineHigh()) {
            // strange error - it should be Low, but now TX is driving line active high
            error = 0x12;
            return 0;
          }
  
          uint16_t res = 0;
  
          for (uint8_t i = 0; i < 6; i++) {
            
            // Waiting for line to get High ;)
            //uint8_t t1=0;
            uint8_t start=TCNT1L;
            while (!_isLineHigh()) {
              /*if (TIFR0 & (1 << TOV0)) {
                TIFR0 = (1 << TOV0);
                t1++;
                if (t1 == 0) { // 256*1.024ms = ~262ms
                  // error - timed-out waiting for TX
                  _pullUp();
                  error = 0x50 | i<<1;
                  return 0;
                }
              }*/
              if (TCNT1L - start > 16) { // every Timer1 tick is 64uS, here we wait for around 1024uS
                // FIXME Make it configurable, so TX can hang for longer than 1ms
                // error - timed-out waiting for TX
                _pullUp();
                error = 0x50 | i<<1;
                return 0;
              }
            }
  
            // Check if TX is pulling line Up or Actively drives it High
            _pullDown();
            if (_isLineLow()) {
              // TX is driving line via PullUp (end of packet)
              _pullUp();
              error = 0;
              return res;
            }
            res <<= 1;
            res |= 1;
  
            // Waiting for line to become Low
            //t1 = 0;
            start=TCNT1L;
            while (_isLineHigh()) {
              /*if (TIFR0 & (1 << TOV0)) {
                TIFR0 = (1 << TOV0);
                t1++;
                if (t1 == 0) { // 256*1.024ms = ~262ms
                  // error - timed-out waiting for TX
                  _pullUp();
                  error = 0x51 | i<<1;
                  return 0;
                }
              }*/
              if (TCNT1L - start > 16) { // every Timer1 tick is 64uS, here we wait for around 1024uS
                // error - timed-out waiting for TX
                _pullUp();
                error = 0x51 | i<<1;
                return 0;
              }
            }
  
            // Check if TX is pulling line Up or Actively drives it Low
            _pullUp();
            if (_isLineHigh()) {
              // TX is driving line via PullUp (end of packet)
              _pullUp();
              error = 0;
              return res;
            }
            res <<= 1;
            
          }
        }
        
        _pullUp();
        error = 0xE1;
        return 0;
      }

      /**
       * error:
       *   0x1x - readPacket error
       *   0x2x - packet is not 0 or 1
       */
      static uint8_t _readByte(uint8_t &error) {
        error = 0xFF;
        uint8_t res = 0;
        for (uint8_t bitNo = 0; bitNo < 8; bitNo++) {
          uint8_t e = 0;
          uint16_t p = _readPacket(e);
          if (e != 0) {
            error = 0x10|bitNo;
            return 0;
          }
          res >>= 1;
          if (p == _PACKET_1) {
            res |= 0x80;
          } else if (p == _PACKET_0) {
          } else {
            error = 0x20|bitNo;
            return 0;
          }
        }
        error = 0;
        return res;
      }

      static bool _writeByte(uint8_t d, uint8_t &error) {
        error = 0xFF;
        uint8_t e = 0;
        
        for (uint8_t bitNo = 0; bitNo < 8; bitNo++) {
          ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
            uint16_t p = _readPacket(e);
            if (e != 0) {
              error = 0x10|bitNo;
              return false;
            }
            if (p != _PACKET_READ_REQ) {
              error = 0x20|bitNo;
              return false;
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
        }
        
        error = 0;
        return true;
      }

      static bool _waitForHandshake() {
        uint8_t start = 0;
        _setupTimer1();
        TIFR1 = (1 << TOV1); // clear Tmr1 Overflow flag
        while (start < HANDSHAKE_TIMEOUT_TMR1_OVERFLOWS) {
          uint8_t err = 0;
          uint16_t res = _readPacket(err);
          if (err == 0 && res == _PACKET_HANDSHAKE) {
            return true;
          }
          if (TIFR1 & (1 << TOV1)) {
            TIFR1 = (1 << TOV1); // clear Tmr1 Overflow flag
            start++;
          }
        }
        return false;
      }

      static bool _isReceivedData;
      static uint8_t _receivedDataLength;
      static bool _isPrevTransmissionFailed;
      static bool _isPrevPrevTransmissionFailed;
      static uint8_t _receivedData[34]; // sys byte + data + crc
      
    public:

      static void setup() {
        _init();
        _isReceivedData = false;
      }
      
      static bool readFrame(uint8_t cmd) {
        _isReceivedData = false;
        for (uint8_t i = 0; i < 34; i++) {
          _receivedData[i] = 0;
        }
        
        ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
          if (!_waitForHandshake()) {
            return false;
          }
          
          uint8_t e = 0;

          // Read Sys Byte
          _receivedData[0] = _readByte(e);
          if (e != 0) {
            return false;
          }

          _receivedDataLength = (_receivedData[0] & 0x1F) + 1; // 0 means 1 byte, 1 -> 2, ... 31 -> 32
          _isPrevTransmissionFailed = (_receivedData[0] & 0x40);
          _isPrevPrevTransmissionFailed = (_receivedData[0] & 0x20);
          
          // Read Data + CRC
          for (uint8_t i = 1; i < _receivedDataLength + 2; i++) {
            _receivedData[i] = _readByte(e);
            if (e != 0) {
              return false;
            }
          }
          
          uint8_t crc = pvt::CRC8::calculate(_receivedData, _receivedDataLength + 1);
          if (crc != _receivedData[_receivedDataLength + 1]) {
            return false;
          }

          // FIXME Implement change in protocol: TX should hold and wait for RX to 
          //          get ready to write data (calculate CRC takes longer than 1uS expected)

          // Write Cmd Byte
          //bool res = _writeByte(cmd, e);
          //if (!res || e != 0) {
          //  return false;
          //}
        }
        
        _isReceivedData = true;
        return true;
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
