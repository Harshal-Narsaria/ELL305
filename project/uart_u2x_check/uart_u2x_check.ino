/**
 * @file uart_u2x_check.ino
 * @brief Normal against double-speed sampling at the same baud rate over the RC line.
 * @author Harshal Narsaria (2024MT60695)
 *
 * @details
 * @verbatim
 * Normal vs double-speed (U2X) sampling at the SAME baud, over the same RC line.
 * Question: on a smeared signal, does it matter where in the bit the receiver samples?
 *   Normal: 16 samples per bit, majority of samples 8, 9, 10
 *   U2X:     8 samples per bit, majority of samples 4, 5, 6
 * The baud is identical in each pair (normal UBRR = n, U2X UBRR = 2n+1), so the waveform on
 * pin 0 is identical too. Any difference between the two rows of a pair comes from the receiver.
 * Each test runs 3 times to show the run-to-run spread.
 *
 * Same polled loop as uart_lean.ino (TX and RX in one loop, no interrupts), counting pattern 0, 1, 2, ...
 * Wiring: pin 1 -> 330 ohm -> pin 0, capacitor from pin 0 to GND. Lift the pin-0 lead while uploading.
 * Serial Monitor at 9600. All tests run first (about 30 s, garbage on the monitor meanwhile),
 * then a clean table repeats every 5 s; copy one block.
 * @endverbatim
 */

#include <avr/io.h>
#include <stdio.h>
#include <util/delay.h>

/**
 * @brief Number of bytes sent in each test.
 */
#define N 10000

/**
 * @brief Number of times each setting is repeated.
 */
#define REPEAT 3

/**
 * @brief One baud-rate setting of the UART.
 *
 * - @c ubrr: value of the baud-rate register UBRR0
 * - @c u2x: true selects double-speed mode (8 samples per bit, not 16)
 *
 * The baud rate is 16 MHz / (16 (ubrr + 1)), or 16 MHz / (8 (ubrr + 1)) with u2x.
 */
struct Setting { uint16_t ubrr; bool u2x; };

/**
 * @brief The baud-rate settings that are tested, in this order.
 */
const Setting settings[] = {
  {15,false},{31,true},                         // 62,500 baud
  {7,false},{15,true},                          // 125,000
  {3,false},{7,true},                           // 250,000
  {1,false},{3,true},                           // 500,000
  {0,false},{1,true},                           // 1,000,000
};

/**
 * @brief Number of entries in ::settings.
 */
const uint8_t COUNT = sizeof(settings)/sizeof(settings[0]);

/**
 * @brief Counts collected by one test.
 *
 * - @c got: bytes received
 * - @c wrong: bytes whose value was not the expected one
 * - @c frame: frame errors (FE flag)
 * - @c over: overruns (DOR flag), each one a lost byte
 */
struct Result { uint16_t got, wrong, frame, over; };

/**
 * @brief Results of all tests, filled in by setup() and printed by loop().
 */
Result results[COUNT][REPEAT];

/**
 * @brief Set up the UART for 8 data bits, no parity and 1 stop bit.
 *
 * The receiver and transmitter are enabled. No UART interrupt is enabled here.
 *
 * @param ubrr Value for the baud-rate register UBRR0.
 * @param u2x true selects double-speed mode.
 */
void uart_init(uint16_t ubrr, bool u2x) {
  UCSR0A = (u2x ? (1<<U2X0) : 0) | (1<<TXC0);
  UBRR0H = ubrr >> 8; UBRR0L = ubrr & 0xFF;
  UCSR0B = (1<<RXEN0) | (1<<TXEN0);
  UCSR0C = (1<<UCSZ01) | (1<<UCSZ00);
}

/**
 * @brief Send one byte, polling until the data register is empty.
 *
 * @param b The byte to send.
 */
void send_byte(uint8_t b){ while(!(UCSR0A & (1<<UDRE0))); UDR0 = b; }

/**
 * @brief Send a zero-terminated string with send_byte().
 *
 * @param s The string to send.
 */
void print_str(const char *s){ while(*s) send_byte(*s++); }

/**
 * @brief Run one test with transmit and receive polled in the same loop.
 *
 * @return The counts of the test.
 */
Result run_test(void) {
  Result r = {0,0,0,0};
  while (UCSR0A & (1<<RXC0)) (void)UDR0;
  uint16_t sent = 0; uint8_t expect = 0; uint32_t idle = 0;
  while (r.got < N && idle < 300000UL) {
    uint8_t st = UCSR0A;                        // one read serves TX and RX
    if (sent < N && (st & (1<<UDRE0))) UDR0 = (uint8_t)sent++;
    if (st & (1<<RXC0)) {
      uint8_t d = UDR0;                         // FE/DOR in st belong to this byte
      if (st & (1<<FE0))  r.frame++;
      if (st & (1<<DOR0)) r.over++;
      if (d != expect) r.wrong++;
      expect = d + 1; r.got++; idle = 0;
    } else idle++;
  }
  while (!(UCSR0A & (1<<TXC0)));
  return r;
}

/**
 * @brief Run every test once, then return the UART to 9600 baud for printing.
 *
 * The Timer0 (millis) interrupt is switched off during the tests, so nothing else uses CPU cycles.
 */
void setup() {
  TIMSK0 = 0;                                   // no millis() interrupt during tests
  for (uint8_t k = 0; k < REPEAT; k++)
    for (uint8_t i = 0; i < COUNT; i++) { uart_init(settings[i].ubrr, settings[i].u2x); results[i][k] = run_test(); }
  uart_init(103, false);
}

/**
 * @brief Print the result table as CSV, then wait 5 s.
 */
void loop() {
  char buf[80];
  print_str("\r\nU2X CHECK (same baud, normal vs U2X sampling, polled, N=10000)\r\n"
            "baud,mode,ubrr,run,got,wrong,frame,over\r\n");
  for (uint8_t i = 0; i < COUNT; i++) {
    const Setting &s = settings[i];
    unsigned long baud = (s.u2x ? 2000000UL : 1000000UL) / (s.ubrr + 1);
    for (uint8_t k = 0; k < REPEAT; k++) {
      const Result &r = results[i][k];
      snprintf(buf, sizeof buf, "%lu,%s,%u,%u,%u,%u,%u,%u\r\n", baud, s.u2x ? "U2X" : "Normal",
               s.ubrr, k + 1, r.got, r.wrong, r.frame, r.over);
      print_str(buf);
    }
  }
  for (uint8_t t = 0; t < 50; t++) _delay_ms(100);   // 5 s
}
