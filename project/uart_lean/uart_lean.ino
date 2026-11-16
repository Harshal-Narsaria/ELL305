/**
 * @file uart_lean.ino
 * @brief Transmit and receive both polled in one loop, with no interrupts.
 * @author Harshal Narsaria (2024MT60695)
 *
 * @details
 * @verbatim
 * Lean UART loopback: TX and RX both polled in one loop, no interrupts.
 * Question: does the UART work at its hardware maximum (2 Mbaud, U2X, UBRR=0)
 * when the software needs fewer cycles per byte than the 80 a frame takes?
 *
 * Wiring: jumper pin 1 (TX) -> pin 0 (RX). Remove the jumper while uploading.
 * Serial Monitor at 9600. The table repeats every 5 s; copy one block.
 *
 * elapsed = CPU cycles from the first UDR0 write until the last byte was received,
 * measured by Timer1 (clk/1024, so resolution 1024 cycles). line_rate = N x cycles
 * per frame, the time for N frames sent back to back with no gaps.
 * elapsed ~= line_rate means TX ran at full speed. "-" = too long for the timer.
 * @endverbatim
 */

#include <avr/io.h>

/**
 * @brief Number of bytes sent in each test.
 */
#define N 10000

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
 * - @c ticks: Timer1 ticks (1024 cycles each) from the first byte sent to the last byte received
 */
struct Result { uint16_t got, wrong, frame, over; uint16_t ticks; };

/**
 * @brief Results of all tests, filled in by setup() and printed by loop().
 */
Result results[COUNT];

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
 * @brief CPU cycles taken by one 10-bit frame.
 *
 * 16 cycles per bit in normal mode or 8 in double-speed mode, times (ubrr + 1).
 *
 * @param s The baud-rate setting.
 * @return Cycles per frame.
 */
uint32_t frame_cycles(const Setting &s) { return (s.u2x ? 80UL : 160UL) * (s.ubrr + 1); }

/**
 * @brief Run one test with transmit and receive polled in the same loop.
 *
 * One read of the status register UCSR0A serves both directions.
 * Timer1 (clock / 1024) measures the time from the first byte sent
 * to the last byte received.
 *
 * @return The counts of the test and the Timer1 reading.
 */
Result run_test(void) {
  Result r = {0,0,0,0,0};
  while (UCSR0A & (1<<RXC0)) (void)UDR0;
  uint16_t sent = 0; uint8_t expect = 0; uint32_t idle = 0;
  TCCR1A = 0; TCNT1 = 0;
  TCCR1B = (1<<CS12) | (1<<CS10);               // start Timer1 at clk/1024
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
  r.ticks = TCNT1;
  TCCR1B = 0;
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
  for (uint8_t i = 0; i < COUNT; i++) { uart_init(settings[i].ubrr, settings[i].u2x); results[i] = run_test(); }
  uart_init(103, false);
  TIMSK0 = (1<<TOIE0);
}

/**
 * @brief Print the result table as CSV, then wait 5 s.
 *
 * The last two columns compare the measured time with the time of N frames sent back to back.
 */
void loop() {
  char buf[100];
  print_str("\r\nLEAN (TX and RX polled, no interrupts)\r\n"
            "baud,mode,ubrr,sent,got,wrong,frame,over,elapsed,line_rate\r\n");
  for (uint8_t i = 0; i < COUNT; i++) {
    const Setting &s = settings[i];
    const Result &r = results[i];
    unsigned long baud = (s.u2x ? 2000000UL : 1000000UL) / (s.ubrr + 1);
    unsigned long line_rate = N * frame_cycles(s);
    int n = snprintf(buf, sizeof buf, "%lu,%s,%u,%u,%u,%u,%u,%u,", baud, s.u2x ? "U2X" : "Normal",
                     s.ubrr, N, r.got, r.wrong, r.frame, r.over);
    if (r.got == N && line_rate < 65000UL * 1024UL)
      snprintf(buf + n, sizeof buf - n, "%lu,%lu\r\n", (unsigned long)r.ticks * 1024UL, line_rate);
    else
      snprintf(buf + n, sizeof buf - n, "-,%lu\r\n", line_rate);
    print_str(buf);
  }
  delay(5000);
}
