// ELP305 Experiment 4: I2C real-time clock (DS1307) with alarm.
// Arduino Uno R3 (ATmega328P, 16 MHz). No RTC library: the DS1307 is read
// and written directly over I2C with Wire, so every bus transfer is visible.
//
// Wiring
//   DS1307 SDA -> A4, SCL -> A5 (4.7 kohm pull-ups to 5 V on both lines),
//   VCC 5 V, GND, 32.768 kHz crystal on X1/X2, 3 V coin cell on VBAT.
//   16x2 LCD (4-bit): RS=D12, EN=D11, DB4..DB7 = D4..D7, RW=GND, VO=pot wiper.
//   Buttons to GND (internal pull-ups): Padj=D2, P+=D9, P-=D10.
//   Buzzer (active, or piezo through 100 ohm) on D8.
//   D3 is not used (faulty input on this particular board).
//
// Padj press 1: enter alarm-setting mode   press 2: set hour
//      press 3: set minute                 press 4: set second
//      press 5: leave alarm-setting mode (alarm armed)
// P+ / P- change the selected unit. Any button silences a ringing alarm.
//
// Serial (9600 baud): one line per minute "RTC hh:mm:ss  millis=..." for the
// time-drift measurement, and "Thhmmss" sets the clock (e.g. T143000).

#include <Wire.h>
#include <LiquidCrystal.h>
#include "types.h"

const uint8_t DS1307_ADDR = 0x68;
LiquidCrystal lcd(12, 11, 4, 5, 6, 7);

const uint8_t PADJ_PIN = 2, PLUS_PIN = 9, MINUS_PIN = 10, BUZZER_PIN = 8;
const unsigned long RING_MS = 30000UL;       // alarm sounds for at most 30 s

Time now = {0, 0, 0};
Time alarmAt = {7, 0, 0};
bool alarmArmed = false;
bool ringing = false;
unsigned long ringStarted = 0;

Mode mode = NORMAL;

// ---------------- DS1307 over I2C ----------------
uint8_t bcd2dec(uint8_t v) { return (v >> 4) * 10 + (v & 0x0F); }
uint8_t dec2bcd(uint8_t v) { return ((v / 10) << 4) | (v % 10); }

// Registers 0..2: seconds (bit 7 = clock halt), minutes, hours (24 h mode).
bool rtcRead(Time &t, bool &halted) {
  Wire.beginTransmission(DS1307_ADDR);
  Wire.write((uint8_t)0x00);                 // register pointer = seconds
  if (Wire.endTransmission() != 0) return false;   // no ACK: chip missing
  if (Wire.requestFrom(DS1307_ADDR, (uint8_t)3) != 3) return false;
  uint8_t sec = Wire.read(), min = Wire.read(), hr = Wire.read();
  halted = sec & 0x80;
  t.s = bcd2dec(sec & 0x7F);
  t.m = bcd2dec(min & 0x7F);
  t.h = bcd2dec(hr & 0x3F);
  return true;
}

void rtcWrite(const Time &t) {
  Wire.beginTransmission(DS1307_ADDR);
  Wire.write((uint8_t)0x00);
  Wire.write(dec2bcd(t.s));                  // bit 7 = 0 starts the oscillator
  Wire.write(dec2bcd(t.m));
  Wire.write(dec2bcd(t.h));                  // bit 6 = 0 selects 24 h mode
  Wire.endTransmission();
}

// Compile time, used once if the clock has never been started.
Time compileTime() {
  const char *c = __TIME__;                  // "hh:mm:ss"
  Time t = { (uint8_t)((c[0]-'0')*10 + c[1]-'0'),
             (uint8_t)((c[3]-'0')*10 + c[4]-'0'),
             (uint8_t)((c[6]-'0')*10 + c[7]-'0') };
  return t;
}

// ---------------- Buttons (debounced, one event per press) ----------------
Button btnAdj   = {PADJ_PIN,  HIGH, HIGH, 0};
Button btnPlus  = {PLUS_PIN,  HIGH, HIGH, 0};
Button btnMinus = {MINUS_PIN, HIGH, HIGH, 0};

bool pressed(Button &b) {
  bool r = digitalRead(b.pin);
  if (r != b.raw) { b.raw = r; b.changed = millis(); }
  if (millis() - b.changed > 30 && b.stable != b.raw) {
    b.stable = b.raw;
    return b.stable == LOW;                  // falling edge = press
  }
  return false;
}

// ---------------- Display ----------------
void print2(uint8_t v) { if (v < 10) lcd.print('0'); lcd.print(v); }

void printTime(const Time &t, int8_t blankField) {
  bool hide = (millis() / 300) % 2;          // blink the field being edited
  if (blankField == 0 && hide) lcd.print("  "); else print2(t.h);
  lcd.print(':');
  if (blankField == 1 && hide) lcd.print("  "); else print2(t.m);
  lcd.print(':');
  if (blankField == 2 && hide) lcd.print("  "); else print2(t.s);
}

void draw(bool rtcOk) {
  lcd.setCursor(0, 0);
  if (!rtcOk) { lcd.print("RTC not found   "); return; }
  lcd.print("Time ");
  printTime(now, -1);
  lcd.print("   ");
  lcd.setCursor(0, 1);
  if (ringing) { lcd.print("** ALARM **     "); return; }
  if (mode == NORMAL) {
    lcd.print(alarmArmed ? "Alm " : "Alm off ");
    if (alarmArmed) { printTime(alarmAt, -1); lcd.print("    "); }
    else lcd.print("        ");
  } else {
    lcd.print(mode == ALARM_MENU ? "Set? " : "Set  ");
    printTime(alarmAt, mode == SET_HOUR ? 0 : mode == SET_MIN ? 1 :
                       mode == SET_SEC ? 2 : -1);
    lcd.print("   ");
  }
}

// ---------------- Alarm setting ----------------
void adjust(int8_t step) {
  switch (mode) {
    case SET_HOUR: alarmAt.h = (alarmAt.h + 24 + step) % 24; break;
    case SET_MIN:  alarmAt.m = (alarmAt.m + 60 + step) % 60; break;
    case SET_SEC:  alarmAt.s = (alarmAt.s + 60 + step) % 60; break;
    default: break;
  }
}

void handleButtons() {
  bool a = pressed(btnAdj);
  bool p = pressed(btnPlus);
  bool m = pressed(btnMinus);
  if (ringing && (a || p || m)) {            // any button silences the alarm
    ringing = false;
    digitalWrite(BUZZER_PIN, LOW);
    return;
  }
  if (a) {
    mode = (Mode)((mode + 1) % 5);           // NORMAL -> menu -> h -> m -> s -> NORMAL
    if (mode == NORMAL) alarmArmed = true;   // 5th press ends setting, arms alarm
  }
  if (p) adjust(+1);
  if (m) adjust(-1);
}

// ---------------- Serial: drift log and time setting ----------------
void handleSerial() {
  static char buf[8];
  static uint8_t n = 0;
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n' || c == '\r') {
      if (n == 7 && buf[0] == 'T') {
        Time t = { (uint8_t)((buf[1]-'0')*10 + buf[2]-'0'),
                   (uint8_t)((buf[3]-'0')*10 + buf[4]-'0'),
                   (uint8_t)((buf[5]-'0')*10 + buf[6]-'0') };
        if (t.h < 24 && t.m < 60 && t.s < 60) { rtcWrite(t); Serial.println(F("time set")); }
      }
      n = 0;
    } else if (n < sizeof buf) buf[n++] = c;
  }
}

void setup() {
  pinMode(PADJ_PIN, INPUT_PULLUP);
  pinMode(PLUS_PIN, INPUT_PULLUP);
  pinMode(MINUS_PIN, INPUT_PULLUP);
  pinMode(BUZZER_PIN, OUTPUT);
  Serial.begin(9600);
  Wire.begin();                              // 100 kHz standard-mode I2C
  lcd.begin(16, 2);

  bool halted = false;
  if (rtcRead(now, halted) && halted) rtcWrite(compileTime());
  Serial.println(F("Exp 4 ready. Thhmmss sets the clock."));
}

void loop() {
  static uint8_t lastSecond = 255, lastMinute = 255;
  bool halted;
  bool ok = rtcRead(now, halted);

  handleButtons();
  handleSerial();

  if (ok && now.s != lastSecond) {           // runs once per RTC second
    lastSecond = now.s;
    if (alarmArmed && mode == NORMAL && !ringing &&
        now.h == alarmAt.h && now.m == alarmAt.m && now.s == alarmAt.s) {
      ringing = true;
      ringStarted = millis();
    }
    if (now.m != lastMinute) {               // drift log: RTC vs Arduino crystal
      lastMinute = now.m;
      Serial.print(F("RTC "));
      if (now.h < 10) Serial.print('0'); Serial.print(now.h); Serial.print(':');
      if (now.m < 10) Serial.print('0'); Serial.print(now.m); Serial.print(':');
      if (now.s < 10) Serial.print('0'); Serial.print(now.s);
      Serial.print(F("  millis=")); Serial.println(millis());
    }
  }

  if (ringing) {
    digitalWrite(BUZZER_PIN, (millis() / 250) % 2);   // 2 Hz beeping
    if (millis() - ringStarted > RING_MS) { ringing = false; digitalWrite(BUZZER_PIN, LOW); }
  }

  draw(ok);
  delay(20);
}
