// ELL/ELP305 Experiment 3 + Going Further part 1 only
// Arduino Uno R3; SSD1306 128x64 I2C OLED at address 0x3C.
// S1=D2 and S0=D3: switches connect their pin to GND when ON.
// FREQ=A0, AMPLITUDE=A1, PHASE=A2, PULSE DUTY=A3. PWM output is D9.

#include <U8g2lib.h>
#include <Wire.h>
#include <avr/interrupt.h>
#include <util/atomic.h>
#include <math.h>

U8G2_SSD1306_128X64_NONAME_1_HW_I2C oled(U8G2_R0, U8X8_PIN_NONE);

// Preserve the supplied reference's switch order:
// 00 square, 01 sine, 10 PWM/pulse, 11 triangle.
enum Wave : byte { SQUARE, SINE, PULSE, TRIANGLE };
const byte S1_PIN = 2, S0_PIN = 3, OUTPUT_PIN = 9;

byte sineTable[256];
volatile byte wave = SINE;
volatile byte amplitude = 200;         // 0..255 -> 0..2.5 V peak
volatile byte phaseOffset = 0;         // 0..255 -> 0..360 degrees
volatile uint16_t pulseHighPositions = 64; // 0..256 -> 0..100% pulse duty
volatile uint32_t phaseAccumulator = 0;
volatile uint32_t phaseStep = 429497UL;

byte waveformSample(byte selectedWave, byte index, uint16_t highPositions) {
  switch (selectedWave) {
    case SINE:
      return sineTable[index];
    case TRIANGLE:
      return index < 128 ? index * 2 : (255 - index) * 2;
    case PULSE:
      return index < highPositions ? 255 : 0;
    default:
      return index < 128 ? 255 : 0;   // square wave
  }
}

byte setAmplitude(byte raw, byte amount) {
  int16_t centred = (int16_t)raw - 128;
  return constrain(128 + ((int32_t)centred * amount) / 255, 0, 255);
}

// A fixed 50 kHz update rate keeps generation independent of OLED speed.
ISR(TIMER2_COMPA_vect) {
  phaseAccumulator += phaseStep;
  byte index = (phaseAccumulator >> 24) + phaseOffset;
  OCR1A = setAmplitude(
      waveformSample(wave, index, pulseHighPositions), amplitude);
}

const char *waveName(byte selected) {
  const char *names[] = { "SQR", "SIN", "PWM", "TRI" };
  return names[selected & 3];
}

void drawGraph(byte selectedWave, byte amount, byte shift,
               int frequency, int amplitudeMillivolts,
               uint16_t highPositions) {
  const byte LEFT = 12, TOP = 11, BOTTOM = 52, MID = 32;

  // Labeled axes. X covers exactly one period; Y follows selected amplitude.
  oled.drawVLine(LEFT, TOP, BOTTOM - TOP + 1);
  oled.drawHLine(LEFT, MID, 128 - LEFT);
  oled.drawPixel(LEFT - 2, TOP);        // Y arrow head
  oled.drawPixel(LEFT + 2, TOP);
  oled.drawPixel(127, MID - 2);        // X arrow head
  oled.drawPixel(127, MID + 2);
  oled.drawStr(1, 17, "+");
  oled.drawStr(1, 51, "-");

  byte oldY = MID;
  for (byte x = LEFT + 1; x < 128; ++x) {
    byte index = (uint16_t)(x - LEFT - 1) * 255 / (126 - LEFT) + shift;
    byte raw = waveformSample(selectedWave, index, highPositions);
    // Automatic Y scaling: any nonzero amplitude uses the available graph height.
    byte y = amount ? BOTTOM - (uint16_t)raw * (BOTTOM - TOP) / 255 : MID;
    if (x > LEFT + 1) oled.drawLine(x - 1, oldY, x, y);
    oldY = y;
  }

  char footer[24];
  snprintf(footer, sizeof(footer), "X:%dms Y:+-%d.%02dV",
           1000 / frequency, amplitudeMillivolts / 1000,
           (amplitudeMillivolts % 1000) / 10);
  oled.drawStr(0, 63, footer);
}

void updateDisplay(byte selectedWave, byte amount, byte shift, int frequency,
                   byte dutyPercent, uint16_t highPositions) {
  int amplitudeMillivolts = amount * 2500L / 255;
  int phaseDegrees = shift * 360L / 256;
  char header[24];
  if (selectedWave == PULSE) {
    snprintf(header, sizeof(header), "PWM %dHz A%d.%02d D%d%%",
             frequency, amplitudeMillivolts / 1000,
             (amplitudeMillivolts % 1000) / 10, dutyPercent);
  } else {
    snprintf(header, sizeof(header), "%s %dHz A%d.%02d PH%d",
             waveName(selectedWave), frequency, amplitudeMillivolts / 1000,
             (amplitudeMillivolts % 1000) / 10, phaseDegrees);
  }

  oled.firstPage();
  do {
    oled.drawStr(0, 8, header);
    drawGraph(selectedWave, amount, shift, frequency, amplitudeMillivolts,
              highPositions);
  } while (oled.nextPage());
}

void setup() {
  pinMode(S1_PIN, INPUT_PULLUP);
  pinMode(S0_PIN, INPUT_PULLUP);
  pinMode(OUTPUT_PIN, OUTPUT);
  Serial.begin(115200);
  oled.begin();
  oled.setFont(u8g2_font_5x7_tf);

  for (int i = 0; i < 256; ++i)
    sineTable[i] = (byte)(127.5 + 127.5 * sin(2.0 * PI * i / 256.0) + 0.5);

  noInterrupts();
  // Timer1 produces 62.5 kHz 8-bit PWM on D9. The RC network averages it.
  TCCR1A = _BV(COM1A1) | _BV(WGM10);
  TCCR1B = _BV(WGM12) | _BV(CS10);
  OCR1A = 128;
  // Timer2 calls the DDS interrupt 50,000 times per second.
  TCCR2A = _BV(WGM21);
  TCCR2B = _BV(CS21);
  OCR2A = 39;
  TIMSK2 = _BV(OCIE2A);
  interrupts();
  Serial.println(
      F("Exp 3 ready: S1/S0 choose wave; A0=freq A1=amp A2=phase A3=duty"));
}

void loop() {
  int frequency = map(analogRead(A0), 0, 1023, 5, 100);
  byte newAmplitude = analogRead(A1) >> 2;
  byte newPhase = analogRead(A2) >> 2;
  byte newDutyPercent = map(analogRead(A3), 0, 1023, 0, 100);
  uint16_t newHighPositions =
      ((uint32_t)newDutyPercent * 256 + 50) / 100;
  byte selectedWave = (digitalRead(S1_PIN) << 1) | digitalRead(S0_PIN);
  uint32_t newStep = ((uint64_t)frequency << 32) / 50000UL;

  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
    wave = selectedWave;
    amplitude = newAmplitude;
    phaseOffset = newPhase;
    pulseHighPositions = newHighPositions;
    phaseStep = newStep;
  }

  // UART is also a simple live numerical readout for checking the controls.
  static unsigned long lastReport = 0;
  if (millis() - lastReport >= 500) {
    lastReport = millis();
    Serial.print(waveName(selectedWave));
    Serial.print(F("  f=")); Serial.print(frequency); Serial.print(F(" Hz"));
    Serial.print(F("  amplitude=")); Serial.print(newAmplitude * 2.5 / 255, 2);
    Serial.print(F(" V  phase=")); Serial.print(newPhase * 360L / 256);
    Serial.print(F(" deg  duty=")); Serial.print(newDutyPercent);
    Serial.println('%');
  }

  updateDisplay(selectedWave, newAmplitude, newPhase, frequency,
                newDutyPercent, newHighPositions);
  delay(80);                           // waveform generation continues in ISR
}
