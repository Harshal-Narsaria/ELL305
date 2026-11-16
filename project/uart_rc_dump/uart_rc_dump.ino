/**
 * @file uart_rc_dump.ino
 * @brief Constant-pattern test for the RC line: shows which bits go wrong.
 * @author Harshal Narsaria (2024MT60695)
 *
 * @details
 * @verbatim
 * Byte dump for the RC wire test: which bits go wrong?
 * Same polled loop as uart_lean.ino, but each test sends ONE constant byte (the pattern) N times,
 * so every received byte can be compared bit by bit with what was sent, even when frames go missing.
 * Received values are counted in a histogram; after the test the counts give the number of
 * 0->1 and 1->0 bit flips and the most common wrong values.
 *
 * Hypothesis from rc_results.csv (330 ohm + 10 nF): the 1 kOhm to the USB chip holds the line's
 * low level at ~1.3 V, so falling edges are slow and 0 bits arrive shortened. If that is right,
 * almost all flips are 0->1 and frame errors stay rare.
 *
 * Patterns, as they appear on the wire (start bit, 8 data bits LSB first, stop bit):
 *   0x55  0 1010101 0 1   every bit alternates: single 0s between 1s (worst case)
 *   0xAA  0 0101010 1 1   same, but the start bit joins data bit 0 into a double 0
 *   0x00  0 0000000 0 1   nine 0s in a row, then the stop bit
 *   0xFF  0 1111111 1 1   the start bit is the only 0
 *
 * Wiring: pin 1 -> 330 ohm -> pin 0, 10 nF from pin 0 to GND. Lift the pin-0 lead while uploading.
 * Serial Monitor at 9600. All tests run first (about 20 s). Pin 1 also feeds the USB chip, so the
 * test traffic shows up as garbage during that time. After it, a clean table repeats every 5 s;
 * copy one block.
 * @endverbatim
 */

#include <avr/io.h>
#include <stdio.h>
#include <util/delay.h>

/**
 * @brief Number of bytes sent in each test.
 */
#define N 2000

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
 * @brief The constant bytes that are sent, one pattern per test.
 */
const uint8_t patterns[] = {0x55, 0xAA, 0x00, 0xFF};

/**
 * @brief Number of entries in ::patterns.
 */
const uint8_t PCOUNT = sizeof(patterns);

/**
 * @brief Counts collected by one test.
 *
 * - @c got: bytes received
 * - @c wrong: bytes whose value was not the expected one
 * - @c frame: frame errors (FE flag)
 * - @c over: overruns (DOR flag), each one a lost byte
 * - @c f01: bits sent as 0 and read as 1
 * - @c f10: bits sent as 1 and read as 0
 * - @c top: the three most common wrong values
 * - @c cnt: how often each value in @c top occurred
 */
struct Result { uint16_t got, wrong, frame, over, f01, f10; uint8_t top[3]; uint16_t cnt[3]; };

/**
 * @brief Results of all tests, filled in by setup() and printed by loop().
 */
Result results[COUNT][PCOUNT];

/**
 * @brief Histogram of received values: hist[v] is the number of received bytes with value v.
 */
uint16_t hist[256];                             // hist[v] = how many received bytes had value v

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
 * @brief Count the 1 bits in a byte.
 *
 * @param v The byte.
 * @return Number of bits that are 1.
 */
uint8_t bits(uint8_t v){ uint8_t n = 0; while (v) { n += v & 1; v >>= 1; } return n; }

/**
 * @brief Send one constant byte N times and analyse what is received.
 *
 * The timed loop is the polled loop of uart_lean.ino.
 * After it, the histogram gives the number of 0-to-1 and 1-to-0 bit flips
 * and the three most common wrong values.
 *
 * @param p The byte to send.
 * @return The counts of the test.
 */
Result run_test(uint8_t p) {
  for (uint16_t v = 0; v < 256; v++) hist[v] = 0;
  while (UCSR0A & (1<<RXC0)) (void)UDR0;
  // Counters are locals so the compiler keeps them in registers (49 cycles per pass, as in uart_lean)
  uint16_t sent = 0, got = 0, frame = 0, over = 0; uint32_t idle = 0;
  while (got < N && idle < 300000UL) {
    uint8_t st = UCSR0A;                        // one read serves TX and RX
    if (sent < N && (st & (1<<UDRE0))) { UDR0 = p; sent++; }
    if (st & (1<<RXC0)) {
      uint8_t d = UDR0;                         // FE/DOR in st belong to this byte
      if (st & (1<<FE0))  frame++;
      if (st & (1<<DOR0)) over++;
      hist[d]++; got++; idle = 0;
    } else idle++;
  }
  while (!(UCSR0A & (1<<TXC0)));

  // Analysis, outside the timed loop
  Result r = {got, (uint16_t)(got - hist[p]), frame, over, 0, 0, {0,0,0}, {0,0,0}};
  for (uint16_t v = 0; v < 256; v++) {
    uint16_t c = hist[v];
    if (!c || v == p) continue;
    r.f01 += c * bits(v & ~p);
    r.f10 += c * bits(~v & p);
    for (uint8_t k = 0; k < 3; k++) {           // keep the 3 most common wrong values
      if (c > r.cnt[k]) {
        for (uint8_t j = 2; j > k; j--) { r.cnt[j] = r.cnt[j-1]; r.top[j] = r.top[j-1]; }
        r.cnt[k] = c; r.top[k] = v;
        break;
      }
    }
  }
  return r;
}

/**
 * @brief Run every test once, then return the UART to 9600 baud for printing.
 *
 * The Timer0 (millis) interrupt is switched off during the tests, so nothing else uses CPU cycles.
 */
void setup() {
  TIMSK0 = 0;                                   // no millis() interrupt: nothing else steals cycles
  for (uint8_t i = 0; i < COUNT; i++)
    for (uint8_t j = 0; j < PCOUNT; j++) {
      uart_init(settings[i].ubrr, settings[i].u2x);
      results[i][j] = run_test(patterns[j]);
    }
  uart_init(103, false);
}

/**
 * @brief Print the result table as CSV, then wait 5 s.
 */
void loop() {
  char buf[120];
  print_str("\r\nRC BYTE DUMP (TX and RX polled, constant pattern, N=2000 per test)\r\n"
            "baud,pattern,got,wrong,frame,over,flips_0to1,flips_1to0,top_wrong\r\n");
  for (uint8_t i = 0; i < COUNT; i++) {
    const Setting &s = settings[i];
    unsigned long baud = (s.u2x ? 2000000UL : 1000000UL) / (s.ubrr + 1);
    for (uint8_t j = 0; j < PCOUNT; j++) {
      const Result &r = results[i][j];
      int n = snprintf(buf, sizeof buf, "%lu,0x%02X,%u,%u,%u,%u,%u,%u,", baud, patterns[j],
                       r.got, r.wrong, r.frame, r.over, r.f01, r.f10);
      for (uint8_t k = 0; k < 3 && r.cnt[k]; k++)
        n += snprintf(buf + n, sizeof buf - n, "0x%02X:%u ", r.top[k], r.cnt[k]);
      snprintf(buf + n, sizeof buf - n, "\r\n");
      print_str(buf);
    }
  }
  for (uint8_t t = 0; t < 50; t++) _delay_ms(100);   // 5 s
}
