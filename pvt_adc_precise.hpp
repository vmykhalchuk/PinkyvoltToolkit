#ifndef PVT_TOOLKIT_ADC_PRECISE_H
#define PVT_TOOLKIT_ADC_PRECISE_H

#include <avr/sleep.h>
#include <avr/interrupt.h>

/**
 * Utility to take highly accurate and precise ADC measurements.
 * To achieve this, util:
 *   - removes all possible noises from MCU by suspending all activities
 *   - puts MCU into sleep during conversion
 *   - discards first measurement (FIXME not implemeneted yet)
 */


  // Additional improvement:
  // Set the reference to INTERNAL (1.1V) for maximum stability
  // Note: If you do this, your input signal must be < 1.1V!
  // If your signal is 0-5V, keep it at default (AVCC).
  // analogReference(INTERNAL);

namespace pvt::toolkit::adc {
  
  // This interrupt fires when the ADC conversion is complete
  //EMPTY_INTERRUPT(ADC_vect);
  
  class ADCPrecise final {
    
    private:
      ADCPrecise() {};
    
    public:
    
      static uint16_t preciseAnalogRead_old(uint8_t pin) {
        ADMUX = (1 << REFS0) | (pin & 0x07); // Set reference to AVCC and select pin
        
        ADCSRA |= (1 << ADIE);    // Enable ADC Interrupt
        set_sleep_mode(SLEEP_MODE_ADC);
        
        sleep_enable();
        sleep_cpu();              // MCU goes to sleep here and ADC starts
        
        // --- MCU is now asleep while ADC works ---
        
        sleep_disable();          // MCU wakes up here
        ADCSRA &= ~(1 << ADIE);   // Disable ADC Interrupt
        
        return ADC;               // Return the 10-bit result
      }

      static uint16_t preciseAnalogRead(uint8_t pin) {
        // 1. Map the Arduino pin (A0-A7) to the internal MUX channel (0-7)
        // On a Nano, A0 is 14, so we subtract 14 or mask with 0x07
        uint8_t channel = pin & 0x07;
      
        // 2. Disable digital input buffer on the specific pin to reduce noise
        uint8_t oldDIDR0 = DIDR0;      // Save current state
        DIDR0 |= (1 << channel);       // Disable digital buffer for this channel
      
        // 3. Configure ADMUX: Reference to AVCC (5V) and select channel
        ADMUX = (1 << REFS0) | (channel & 0x07);
        
        // 4. Enable ADC, Enable ADC Interrupt
        ADCSRA |= (1 << ADEN) | (1 << ADIE);
        
        // 5. Prepare and enter Sleep Mode (ADC Noise Reduction)
        set_sleep_mode(SLEEP_MODE_ADC);
        sleep_enable();
        
        // Disable interrupts globally before sleep to ensure we don't 
        // miss the wake-up trigger or race into sleep.
        noInterrupts(); // FIXME Make sure we restore interrupts only if they were enabled. Better use ATOMIC_BLOCK
        if (true) { 
          interrupts();
          sleep_cpu(); 
        }
        
        // --- MCU is now asleep while ADC conversion happens ---
      
        // 6. Wake up and clean up
        sleep_disable();
        ADCSRA &= ~(1 << ADIE);        // Disable ADC Interrupt
        DIDR0 = oldDIDR0;              // Restore digital buffer settings
      
        return ADC;                    // Return the 10-bit result (ADCL + ADCH)
      }
    
  };
  
};

namespace pvt {
  
  using ADCPrecise = pvt::toolkit::adc::ADCPrecise;
};


#endif
