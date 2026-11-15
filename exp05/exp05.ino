// Arduino Uno / classic Nano, ATmega328P at 16 MHz.
// D2->DAC pin12, D3->11, D4->10, D5->9,
// D6->8, D7->7, D8->6, D9->5 (DAC0808).
// Pot wipers: A0 = amplitude, A1 = pitch. Pot ends: 5V and GND.
#include <Arduino.h>
#include <avr/interrupt.h>

// One hardcoded sine-wave cycle, centered around zero.
const int8_t wave[16] = {
  0, 49, 90, 117, 127, 117, 90, 49,
  0, -49, -90, -117, -127, -117, -90, -49
};

// Twinkle, Twinkle, Little Star (first two lines), in Hz.
const uint16_t melody[] = {
  262, 262, 392, 392, 440, 440, 392,
  349, 349, 330, 330, 294, 294, 262
};

volatile uint16_t phase = 0, phaseStep = 0;
volatile uint8_t amplitude = 0;

void writeDAC(uint8_t value) {
  PORTD = (PORTD & 0x03) | ((value & 0x3F) << 2); // D2-D7
  PORTB = (PORTB & 0xFC) | (value >> 6);          // D8-D9
}

// Output one audio sample every 62.5 us (16,000 samples/second).
ISR(TIMER1_COMPA_vect) {
  phase += phaseStep;
  int sample = wave[phase >> 12];
  writeDAC(128 + sample * amplitude / 127);
}

void setup() {
  for (uint8_t pin = 2; pin <= 9; pin++) pinMode(pin, OUTPUT);
  writeDAC(128);

  noInterrupts();
  TCCR1A = 0;
  TCCR1B = 0;
  TCNT1 = 0;
  OCR1A = 999;                 // 16 MHz / (999 + 1) = 16 kHz
  TIFR1 = _BV(OCF1A);
  TIMSK1 = _BV(OCIE1A);
  TCCR1B = _BV(WGM12) | _BV(CS10);
  interrupts();
}

void loop() {
  for (uint8_t note = 0; note < 14; note++) {
    unsigned long duration = (note == 6 || note == 13) ? 600 : 300;
    unsigned long started = millis();

    while (millis() - started < duration) {
      uint8_t volume = map(analogRead(A0), 0, 1023, 0, 127);
      unsigned long pitch = map(analogRead(A1), 0, 1023, 50, 200);
      unsigned long frequency = melody[note] * pitch / 100;
      uint16_t step = frequency * 65536UL / 16000UL;

      noInterrupts();           // Update the shared settings together.
      amplitude = volume;
      phaseStep = step;
      interrupts();
      delay(20);                // Read both knobs every 20 ms.
    }

    amplitude = 0;
    delay(40);                  // Small gap between notes.
  }
}
