// Types used by exp04.ino. They live in a header because the Arduino IDE
// inserts function prototypes at the top of the .ino, before any struct
// defined there, which would make Time/Button unknown in those prototypes.
#pragma once
#include <Arduino.h>

struct Time { uint8_t h, m, s; };
struct Button { uint8_t pin; bool raw, stable; unsigned long changed; };
enum Mode : uint8_t { NORMAL, ALARM_MENU, SET_HOUR, SET_MIN, SET_SEC };
