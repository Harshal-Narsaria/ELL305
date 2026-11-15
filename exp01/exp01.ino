#include <LiquidCrystal.h>
#include <DHT.h>

// RS, EN, D4, D5, D6, D7
LiquidCrystal lcd(12, 11, 4, 5, 6, 7);

#define DHTPIN 2
#define DHTTYPE DHT22
DHT dht(DHTPIN, DHTTYPE);

#define LED_HIGH 8 
#define LED_LOW  9 

float upperLimit = 30.0;
float lowerLimit = 20.0;

void setup() {
  lcd.begin(16, 2);
  dht.begin();

  pinMode(LED_HIGH, OUTPUT);
  pinMode(LED_LOW, OUTPUT);
}

void loop() {
  float temperature = dht.readTemperature();

  if (isnan(temperature)) {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Sensor Error");
    delay(1000);
    return;
  }

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Temp: ");
  lcd.print(temperature);
  lcd.print(" C");

  if (temperature > upperLimit) {
    digitalWrite(LED_HIGH, HIGH);
    digitalWrite(LED_LOW, LOW);
    lcd.setCursor(0, 1);
    lcd.print("Status: HIGH");
  } 
  else if (temperature < lowerLimit) {
    digitalWrite(LED_HIGH, LOW);
    digitalWrite(LED_LOW, HIGH);
    lcd.setCursor(0, 1);
    lcd.print("Status: LOW");
  } 
  else {
    digitalWrite(LED_HIGH, LOW);
    digitalWrite(LED_LOW, LOW);
    lcd.setCursor(0, 1);
    lcd.print("Status: NORMAL");
  }

  delay(1000);
}