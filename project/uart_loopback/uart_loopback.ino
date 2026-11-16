/**
 * @file uart_loopback.ino
 * @brief Interrupt-driven transmit and polled receive in loopback (the main measurement).
 * @author Harshal Narsaria (2024MT60695)
 *
 * @details
 * The transmit interrupt sends N bytes of a counting pattern (0, 1, 2, ...) at line rate.
 * The main loop polls the receiver, checks every byte and counts frame errors,
 * overruns and wrong values. The test is repeated for nine baud rates,
 * from 9600 baud to 2 Mbaud.
 *
 * Wiring: jumper pin 1 (TX) to pin 0 (RX). Remove the jumper while uploading.
 * Serial Monitor at 9600 baud. The result table repeats every 5 s.
 */

#include <avr/io.h>
#include <avr/interrupt.h>

/**
 * @brief Number of bytes sent in each test.
 */
#define N 10000

/**
 * @brief Extra no-operation instructions run for each received byte (changed for each run).
 */
#define WORK 0           // change each run

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
Setting settings[] = {
  {103,false},{51,false},{16,false},{8,false},{16,true},{3,false},{1,false},{0,false},{0,true},
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
Result results[sizeof(settings)/sizeof(settings[0])];

/**
 * @brief Number of bytes handed to the UART so far.
 *
 * Shared between the transmit interrupt and the main loop, so it is volatile.
 */
volatile uint16_t sent_count;

/**
 * @brief Transmit interrupt (UART data register empty).
 *
 * Stores the next byte of the counting pattern into UDR0.
 * It disables itself after N bytes.
 */
ISR(USART_UDRE_vect) {
  UDR0 = (uint8_t)sent_count;
  if (++sent_count >= N) UCSR0B &= ~(1 << UDRIE0);
}

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
 * @brief Run one test: transmit by interrupt, receive by polling.
 *
 * Each received byte is compared with the next value of the counting pattern.
 * The test ends when N bytes have arrived, or when nothing has arrived
 * for 300,000 passes of the loop.
 *
 * @return The counts of the test.
 */
Result run_test(void) {
  Result r = {0,0,0,0};
  while (UCSR0A & (1<<RXC0)) (void)UDR0;
  uint8_t expect = 0; uint32_t idle = 0;
  sent_count = 0;
  UCSR0B |= (1<<UDRIE0);
  while (r.got < N && idle < 300000UL) {
    if (UCSR0A & (1<<RXC0)) {
      uint8_t status = UCSR0A; uint8_t d = UDR0;
      if (status & (1<<FE0))  r.frame++;
      if (status & (1<<DOR0)) r.over++;
      if (d != expect) r.wrong++;
      expect = d + 1; r.got++; idle = 0;
      for (uint16_t k = 0; k < WORK; k++) asm volatile("nop");
    } else idle++;
  }
  UCSR0B &= ~(1<<UDRIE0);
  while (!(UCSR0A & (1<<TXC0)));
  return r;
}

/**
 * @brief Run every test once, then return the UART to 9600 baud for printing.
 *
 * The Timer0 (millis) interrupt is switched off during the tests, so nothing else uses CPU cycles.
 */
void setup() {
  TIMSK0 = 0;
  for (uint8_t i = 0; i < COUNT; i++) { uart_init(settings[i].ubrr, settings[i].u2x); results[i] = run_test(); }
  uart_init(103, false);
  TIMSK0 = (1<<TOIE0);
}

/**
 * @brief Print the result table as CSV, then wait 5 s.
 */
void loop() {
  char buf[80];
  snprintf(buf, sizeof buf, "\r\nWORK=%d\r\nbaud,mode,ubrr,sent,got,wrong,frame,over\r\n", WORK);
  print_str(buf);
  for (uint8_t i = 0; i < COUNT; i++) {
    unsigned long baud = (settings[i].u2x ? 2000000UL : 1000000UL) / (settings[i].ubrr + 1);
    snprintf(buf, sizeof buf, "%lu,%s,%u,%u,%u,%u,%u,%u\r\n", baud, settings[i].u2x ? "U2X":"Normal",
             settings[i].ubrr, N, results[i].got, results[i].wrong, results[i].frame, results[i].over);
    print_str(buf);
  }
  delay(5000);
}
