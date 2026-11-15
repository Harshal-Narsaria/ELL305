#include <Keypad.h>
#include <LiquidCrystal.h>
#include <math.h>

// Existing Experiment-2 wiring (unchanged).
LiquidCrystal lcd(8, 2, 10, 11, 12, 13);
const byte ROWS = 4, COLS = 4;
char keys[ROWS][COLS] = {
  {'1','2','3','+'}, {'4','5','6','-'},
  {'7','8','9','*'}, {'.','0','=','/'}
};
byte rowPins[ROWS] = {A5, A4, A3, A2};
byte colPins[COLS] = {7, 6, 5, 4};
Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);

// Three push buttons go from these pins to GND (INPUT_PULLUP).
const byte FUNCTION_PIN = 9;   // next key uses the function layer
const byte HISTORY_PIN  = A0;  // enter/leave history
const byte MODE_PIN     = A1;  // calculator <-> digi-lock
const byte UNLOCK_LED   = 3;   // D3 -> 220 ohm -> LED -> GND

enum Mode { CALCULATOR, DIGILOCK };
Mode mode = CALCULATOR;
String expression;
bool resultShown = false, functionLayer = false, historyMode = false;
bool unlocked = false;
String passwordInput;
const char *PASSWORDS[] = {"12A3", "45B6", "78C9"};
byte failedAttempts = 0;
bool blocked = false;
unsigned long blockedAt = 0;

const byte MAX_EXPRESSION = 40;
const byte HISTORY_SIZE = 5;
String history[HISTORY_SIZE];
byte historyCount = 0, historyView = 0;
byte scrollOffset = 0;
unsigned long scrollAt = 0;

struct Button { byte pin; bool raw, stable; unsigned long changed; };
Button functionButton = {FUNCTION_PIN, HIGH, HIGH, 0};
Button historyButton  = {HISTORY_PIN,  HIGH, HIGH, 0};
Button modeButton     = {MODE_PIN,     HIGH, HIGH, 0};

const char *parseAt;
bool parseOK;
byte parseDepth;
const char *parseError;

void fail(const char *message) { if (parseOK) parseError = message; parseOK = false; }

void showExpression() {
  lcd.clear();
  String shown = expression;
  if (shown.length() > 16) shown = shown.substring(shown.length() - 16);
  lcd.print(shown);
}

bool pressed(Button &b) {
  bool now = digitalRead(b.pin);
  if (now != b.raw) { b.raw = now; b.changed = millis(); }
  if (now != b.stable && millis() - b.changed >= 30) {
    b.stable = now;
    return now == LOW;
  }
  return false;
}

void addHistory(const String &entry) {
  if (historyCount < HISTORY_SIZE) history[historyCount++] = entry;
  else {
    for (byte i = 1; i < HISTORY_SIZE; ++i) history[i - 1] = history[i];
    history[HISTORY_SIZE - 1] = entry;
  }
  Serial.print(millis()); Serial.print("\tCALC\t"); Serial.println(entry);
}

void logEdit() {
  Serial.print(millis()); Serial.print("\tEDIT\t"); Serial.println(expression);
}

void displayHistory() {
  lcd.clear();
  lcd.print("H "); lcd.print(historyView + 1); lcd.print('/'); lcd.print(historyCount); lcd.print(" -old +new");
  lcd.setCursor(0, 1);
  lcd.print(history[historyView].substring(scrollOffset, scrollOffset + 16));
}

void skipSpaces() { while (*parseAt == ' ') ++parseAt; }
float parseExpression();

float parseNumber() {
  skipSpaces();
  bool haveDigit = false;
  float value = 0, scale = 0.1;
  while (*parseAt >= '0' && *parseAt <= '9') {
    haveDigit = true; value = value * 10 + (*parseAt++ - '0');
  }
  if (*parseAt == '.') {
    ++parseAt;
    while (*parseAt >= '0' && *parseAt <= '9') {
      haveDigit = true; value += (*parseAt++ - '0') * scale; scale *= 0.1;
    }
  }
  if (!haveDigit) fail("Syntax error");
  // Scientific notation supports large/small results and chaining them.
  if (*parseAt == 'e' || *parseAt == 'E') {
    ++parseAt; bool negative = false;
    if (*parseAt == '+' || *parseAt == '-') negative = (*parseAt++ == '-');
    if (*parseAt < '0' || *parseAt > '9') fail("Bad exponent");
    int exponent = 0;
    while (*parseAt >= '0' && *parseAt <= '9') {
      exponent = exponent * 10 + (*parseAt++ - '0');
      if (exponent > 45) { fail("Range error"); return 0; }
    }
    // Repeated scaling avoids forming 10^39 as an intermediate float.
    while (exponent--) value = negative ? value / 10 : value * 10;
  }
  if (!isfinite(value)) fail("Overflow");
  return value;
}

float parsePrimary();
float primaryBody() {
  skipSpaces();
  if (*parseAt == '+') { ++parseAt; return parsePrimary(); }
  if (*parseAt == '-') { ++parseAt; return -parsePrimary(); }
  if (strncmp(parseAt, "sqrt(", 5) == 0) {
    parseAt += 5;
    float v = parseExpression();
    if (*parseAt != ')') { fail("Missing )"); return 0; }
    if (v < 0) { fail("Negative sqrt"); return 0; }
    ++parseAt; return sqrt(v);
  }
  if (*parseAt == '(') {
    ++parseAt;
    float v = parseExpression();
    if (*parseAt != ')') fail("Missing )"); else ++parseAt;
    return v;
  }
  return parseNumber();
}

float parsePrimary() {
  if (!parseOK) return 0;
  if (parseDepth >= 6) { fail("Too much nesting"); return 0; }
  ++parseDepth; float value = primaryBody(); --parseDepth;
  return value;
}

float parseTerm() {
  float v = parsePrimary();
  while (parseOK) {
    skipSpaces(); char op = *parseAt;
    if (op != '*' && op != '/') break;
    ++parseAt; float rhs = parsePrimary();
    if (op == '*') v *= rhs;
    else if (rhs == 0) { fail("Divide by zero"); return 0; }
    else v /= rhs;
    if (!isfinite(v)) fail("Overflow");
  }
  return v;
}

float parseExpression() {
  float v = parseTerm();
  while (parseOK) {
    skipSpaces(); char op = *parseAt;
    if (op != '+' && op != '-') break;
    ++parseAt; float rhs = parseTerm();
    v = (op == '+') ? v + rhs : v - rhs;
    if (!isfinite(v)) fail("Overflow");
  }
  return v;
}

bool evaluate(const String &text, float &answer) {
  parseAt = text.c_str(); parseOK = true; parseDepth = 0; parseError = "Syntax error";
  if (!text.length() || text.length() > MAX_EXPRESSION) fail("Empty/too long");
  answer = parseExpression(); skipSpaces();
  if (*parseAt != '\0') fail("Syntax error");
  if (!isfinite(answer)) fail("Overflow");
  return parseOK;
}

String formatNumber(float value) {
  if (value == 0) return "0";
  char buffer[24];
  float magnitude = fabs(value);
  if (magnitude >= 0.0001 && magnitude < 10000000) {
    int decimals = 6 - (int)floor(log10(magnitude));
    dtostrf(value, 1, decimals, buffer);
    String result(buffer);
    if (result.indexOf('.') >= 0) {
      while (result.endsWith("0")) result.remove(result.length() - 1);
      if (result.endsWith(".")) result.remove(result.length() - 1);
    }
    return result;
  }
  dtostre(value, buffer, 6, 0); // seven significant digits; fits the LCD
  return String(buffer);
}

void evaluateAndDisplay() {
  float answer;
  lcd.setCursor(0, 1); lcd.print("                "); lcd.setCursor(0, 1);
  if (!evaluate(expression, answer)) {
    lcd.print(parseError); addHistory(expression + "=" + parseError);
  } else {
    String result = formatNumber(answer);
    lcd.print('='); lcd.print(result.substring(0, 15));
    addHistory(expression + "=" + result);
  }
  resultShown = true;
}

String lastResult;
bool lastSuccess = false;

void appendInput(const String &text) {
  if (expression.length() + text.length() > MAX_EXPRESSION) {
    lcd.setCursor(0, 1); lcd.print("Input full (40)"); return;
  }
  expression += text; showExpression(); logEdit();
}

void calculatorKey(char key) {
  if (historyMode) {
    if (!historyCount) return;
    if (key == '+' && historyView + 1 < historyCount) ++historyView;
    if (key == '-' && historyView > 0) --historyView;
    scrollOffset = 0; scrollAt = millis(); displayHistory(); return;
  }
  if (functionLayer) {
    functionLayer = false;
    if (resultShown && (key == '1' || key == '2')) expression = "";
    resultShown = false;
    if (key == '1') { appendInput("sqrt("); return; }
    else if (key == '2') { appendInput("("); return; }
    else if (key == '3') { appendInput(")"); return; }
    else if (key == '4') { appendInput("e"); return; }
    else if (key == '.') { expression = ""; resultShown = false; }
    else if (key == '0' && expression.length()) expression.remove(expression.length() - 1);
    showExpression(); logEdit(); return;
  }
  if (key == '=') {
    if (resultShown) return;
    evaluateAndDisplay();
    float value; lastSuccess = evaluate(expression, value);
    if (lastSuccess) lastResult = formatNumber(value);
    return;
  }
  if (resultShown) {
    if ((key >= '0' && key <= '9') || key == '.' || !lastSuccess) expression = "";
    else expression = lastResult;
  }
  resultShown = false;
  appendInput(String(key));
}

void showLockPrompt() {
  lcd.clear(); lcd.print("PIN then ="); lcd.setCursor(0, 1);
  if (blocked) { lcd.clear(); lcd.print("LOCKED 15 sec"); return; }
  for (byte i = 0; i < passwordInput.length(); ++i) lcd.print('*');
}

void lockKey(char key) {
  if (blocked) return;
  if (unlocked) { unlocked = false; digitalWrite(UNLOCK_LED, LOW); passwordInput = ""; showLockPrompt(); return; }
  if (key == '=') {
    bool correct = false;
    for (byte i = 0; i < 3; ++i) if (passwordInput == PASSWORDS[i]) correct = true;
    lcd.clear();
    if (correct) {
      unlocked = true; failedAttempts = 0; digitalWrite(UNLOCK_LED, HIGH);
      lcd.print("ACCESS GRANTED"); lcd.setCursor(0, 1); lcd.print("Any key locks");
      Serial.print(millis()); Serial.println("\tLOCK\tACCESS GRANTED");
    } else {
      ++failedAttempts; lcd.print("ACCESS DENIED");
      Serial.print(millis()); Serial.println("\tLOCK\tACCESS DENIED");
      if (failedAttempts >= 3) { blocked = true; blockedAt = millis(); failedAttempts = 0; lcd.setCursor(0, 1); lcd.print("Wait 15 seconds"); }
      else { lcd.setCursor(0, 1); lcd.print("Try again"); }
      passwordInput = "";
    }
    return;
  }
  if (key == '.') passwordInput = "";
  else if (passwordInput.length() < 12) {
    if (key == '+') key = 'A'; else if (key == '-') key = 'B';
    else if (key == '*') key = 'C'; else if (key == '/') key = 'D';
    passwordInput += key;
  }
  showLockPrompt();
}

void setup() {
  Serial.begin(9600);
  lcd.begin(16, 2);
  pinMode(FUNCTION_PIN, INPUT_PULLUP); pinMode(HISTORY_PIN, INPUT_PULLUP);
  pinMode(MODE_PIN, INPUT_PULLUP); pinMode(UNLOCK_LED, OUTPUT);
  digitalWrite(UNLOCK_LED, LOW);
  expression.reserve(MAX_EXPRESSION + 1); passwordInput.reserve(13); lastResult.reserve(24);
  for (byte i = 0; i < HISTORY_SIZE; ++i) history[i].reserve(64);
  lcd.print("Calculator ready");
  Serial.println("uptime_ms\ttype\ttransaction");
}

void loop() {
  // Poll every button in every mode, to avoid carrying a stale press.
  bool modePressed = pressed(modeButton);
  bool functionPressed = pressed(functionButton);
  bool historyPressed = pressed(historyButton);
  if (blocked && millis() - blockedAt >= 15000UL) {
    blocked = false;
    if (mode == DIGILOCK) showLockPrompt();
  }
  if (modePressed) {
    mode = (mode == CALCULATOR) ? DIGILOCK : CALCULATOR;
    historyMode = functionLayer = false; passwordInput = "";
    unlocked = false; digitalWrite(UNLOCK_LED, LOW);
    resultShown = false;
    if (mode == CALCULATOR) showExpression(); else showLockPrompt();
  }
  if (mode == CALCULATOR && functionPressed && !historyMode) {
    functionLayer = !functionLayer;
    if (functionLayer) { lcd.clear(); lcd.print("1:SQ 2:( 3:) 4:e"); lcd.setCursor(0,1); lcd.print(".:clear 0:del"); }
    else showExpression();
  }
  if (mode == CALCULATOR && historyPressed) {
    functionLayer = false; scrollOffset = 0; scrollAt = millis();
    historyMode = !historyMode;
    if (historyMode && historyCount) { historyView = historyCount - 1; displayHistory(); }
    else if (historyMode) { lcd.clear(); lcd.print("History empty"); }
    else showExpression();
  }
  if (historyMode && historyCount && millis() - scrollAt >= 700UL) {
    scrollAt = millis();
    byte length = history[historyView].length();
    if (length > 16) { scrollOffset = (scrollOffset < length - 16) ? scrollOffset + 1 : 0; displayHistory(); }
  }
  char key = keypad.getKey();
  if (key) { if (mode == CALCULATOR) calculatorKey(key); else lockKey(key); }
}
