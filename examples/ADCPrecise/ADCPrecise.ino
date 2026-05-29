#include <pvt_adc_precise.hpp>

constexpr int ADC_MONITOR_PIN = A0;

void setup() {

  // Read ADC with highest precision possible
  uint16_t v = pvt::ADCPrecise::preciseAnalogRead(ADC_MONITOR_PIN);
  
}

void loop() {
}