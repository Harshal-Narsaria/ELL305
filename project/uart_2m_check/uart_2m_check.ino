/**
 * @file uart_2m_check.ino
 * @brief Check that transmission stays back to back at 2 Mbaud while the receiver loses bytes.
 * @author Harshal Narsaria (2024MT60695)
 *
 * @details
 * @verbatim
 * 2 Mbaud check: does TX stay back to back when the receiver is losing bytes?
 * Repeats 2M (U2X), 1M (U2X) and 1M (normal) 5 times each, interleaved, and records when the
 * LAST byte entered the TX shift register (Timer1, clk/64, so 64-cycle resolution).
 * If TX is back to back, that time is (N-1) frames: 799,920 cycles at 2M, 1,599,840 at 1M,
 * whether or not bytes are lost on the receiving side. "tx_excess" = measured - ideal, in cycles;
 * anything clearly above ~+64 (plus the first frame's start-up offset) means gaps on the wire.
 *
 * Same polled loop as uart_lean.ino (TX and RX in one loop, no interrupts), counting pattern 0, 1, 2, ...
 * The TX test is written so the timestamp costs nothing while bytes are still being sent.
 * Wiring: pin 1 -> 330 ohm -> pin 0, capacitor from pin 0 to GND (the same 10 nF as before).
 * Lift the pin-0 lead while uploading, plug it back, then press RESET.
 * Serial Monitor at 9600. All tests run first (about 15 s, garbage on the monitor meanwhile),
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
#define REPEAT 5

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
  {0,true},                                     // 2,000,000 (U2X)
  {1,true},                                     // 1,000,000 (U2X)
  {0,false},                                    // 1,000,000 (normal)
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
 * - @c tx_ticks: Timer1 ticks (64 cycles each) when the last byte entered the transmit shift register
 */
struct Result { uint16_t got, wrong, frame, over, tx_ticks; };

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
 * @brief CPU cycles taken by one 10-bit frame.
 *
 * 16 cycles per bit in normal mode or 8 in double-speed mode, times (ubrr + 1).
 *
 * @param s The baud-rate setting.
 * @return Cycles per frame.
 */
uint32_t frame_cycles(const Setting &s) { return (s.u2x ? 80UL : 160UL) * (s.ubrr + 1); }

/**
 * @brief Run one polled test and record when the last byte was handed to the transmitter.
 *
 * Timer1 (clock / 64) is read once, when the last byte has moved into the shift register.
 *
 * @return The counts of the test and the Timer1 reading.
 */
Result run_test(void) {
  while (UCSR0A & (1<<RXC0)) (void)UDR0;
  // Counters are locals so the compiler keeps them in registers (a struct here costs ~12 extra cycles per byte)
  uint16_t sent = 0, tx_ticks = 0, got = 0, wrong = 0, frame = 0, over = 0;
  uint8_t expect = 0; uint32_t idle = 0;
  TCCR1A = 0; TCNT1 = 0;
  TCCR1B = (1<<CS11) | (1<<CS10);               // start Timer1 at clk/64
  while (got < N && idle < 300000UL) {
    uint8_t st = UCSR0A;                        // one read serves TX and RX
    if (st & (1<<UDRE0)) {
      if (sent < N) UDR0 = (uint8_t)sent++;
      else if (!tx_ticks) tx_ticks = TCNT1;     // last byte has just moved into the shift register
    }
    if (st & (1<<RXC0)) {
      uint8_t d = UDR0;                         // FE/DOR in st belong to this byte
      if (st & (1<<FE0))  frame++;
      if (st & (1<<DOR0)) over++;
      if (d != expect) wrong++;
      expect = d + 1; got++; idle = 0;
    } else idle++;
  }
  TCCR1B = 0;
  while (!(UCSR0A & (1<<TXC0)));
  Result r = {got, wrong, frame, over, tx_ticks};
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
 *
 * The last column is the measured transmit time minus the ideal time of N - 1 frames.
 */
void loop() {
  char buf[100];
  print_str("\r\n2M CHECK (polled, N=10000; tx = cycles until the last byte entered the shift register)\r\n"
            "baud,mode,ubrr,run,got,wrong,frame,over,tx_cycles,tx_ideal,tx_excess\r\n");
  for (uint8_t i = 0; i < COUNT; i++) {
    const Setting &s = settings[i];
    unsigned long baud = (s.u2x ? 2000000UL : 1000000UL) / (s.ubrr + 1);
    long ideal = (long)(N - 1) * (long)frame_cycles(s);
    for (uint8_t k = 0; k < REPEAT; k++) {
      const Result &r = results[i][k];
      long tx = (long)r.tx_ticks * 64L;
      snprintf(buf, sizeof buf, "%lu,%s,%u,%u,%u,%u,%u,%u,%ld,%ld,%ld\r\n", baud, s.u2x ? "U2X" : "Normal",
               s.ubrr, k + 1, r.got, r.wrong, r.frame, r.over, tx, ideal, tx - ideal);
      print_str(buf);
    }
  }
  for (uint8_t t = 0; t < 50; t++) _delay_ms(100);   // 5 s
}
