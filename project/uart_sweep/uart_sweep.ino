/**
 * @file uart_sweep.ino
 * @brief Sweep of the extra work per byte: finds how much spare time each baud rate leaves.
 * @author Harshal Narsaria (2024MT60695)
 *
 * @details
 * @verbatim
 * UART loopback WORK sweep: one upload measures every baud setting.
 * Wiring: jumper pin 1 (TX) -> pin 0 (RX). Remove the jumper while uploading.
 * Serial Monitor at 9600. Opening it resets the board and restarts the sweep.
 * The whole run takes about 6 minutes and ends with a DONE line.
 *
 * Each baud setting gets:
 *   grid   rows: fixed WORK values (to fit the overload fraction model)
 *   bisect rows: binary search for the largest WORK with zero errors
 *   thresh row : the result of that search
 *
 * TX is fed by the UDRE interrupt at line rate; the main loop polls RX.
 * busy(n) is inline asm so its cost is fixed by the instruction timings:
 * nop (1) + sbiw (2) + brne (2 taken, 1 not) = 5n - 1 cycles for n > 0.
 * @endverbatim
 */

#include <avr/io.h>
#include <avr/interrupt.h>

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
 * @brief Fixed work values measured at every baud rate.
 */
const uint16_t grid[] = {0, 10, 20, 40, 80, 160, 320};

/**
 * @brief Number of entries in ::grid.
 */
const uint8_t GRID_COUNT = sizeof(grid)/sizeof(grid[0]);

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
 * @brief Use up a known number of CPU cycles.
 *
 * Written in assembly so that its cost is fixed: 5n - 1 cycles for n > 0, and none for n = 0.
 *
 * @param n Number of passes of the delay loop.
 */
static inline void busy(uint16_t n) {
  if (n) asm volatile("1: nop\n\tsbiw %0, 1\n\tbrne 1b" : "+w"(n));
}

/**
 * @brief Run one test: transmit by interrupt, receive by polling, with extra work per byte.
 *
 * @param work Argument passed to busy() after each received byte.
 * @return The counts of the test.
 */
Result run_test(uint16_t work) {
  Result r = {0,0,0,0};
  delayMicroseconds(200);                       // let any echo of the last print finish
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
      busy(work);
    } else idle++;
  }
  UCSR0B &= ~(1<<UDRIE0);
  while (!(UCSR0A & (1<<TXC0)));
  return r;
}

/**
 * @brief Check whether a test received every byte without error.
 *
 * @param r The counts of the test.
 * @return true if all N bytes arrived with no wrong value, frame error or overrun.
 */
bool passed(const Result &r) { return r.got == N && r.wrong == 0 && r.frame == 0 && r.over == 0; }

/**
 * @brief CPU cycles taken by one 10-bit frame.
 *
 * 16 cycles per bit in normal mode or 8 in double-speed mode, times (ubrr + 1).
 *
 * @param s The baud-rate setting.
 * @return Cycles per frame.
 */
uint16_t frame_cycles(const Setting &s) { return (s.u2x ? 80U : 160U) * (s.ubrr + 1); }

/**
 * @brief Print one line at 9600 baud.
 *
 * Switches the UART to 9600 baud, prints, and waits until the last bit has left the pin.
 *
 * @param line The text to print.
 */
void report(const char *line) {
  uart_init(103, false);
  print_str(line);
  while (!(UCSR0A & (1<<TXC0)));
}

/**
 * @brief Print one CSV row of results.
 *
 * @param kind Row type: grid or bisect.
 * @param s The baud-rate setting of the test.
 * @param work The work value of the test.
 * @param r The counts of the test.
 */
void print_row(const char *kind, const Setting &s, uint16_t work, const Result &r) {
  char buf[96];
  unsigned long baud = (s.u2x ? 2000000UL : 1000000UL) / (s.ubrr + 1);
  snprintf(buf, sizeof buf, "%s,%lu,%s,%u,%u,%u,%u,%u,%u,%u,%u,%d\r\n", kind, baud, s.u2x ? "U2X" : "Normal",
           s.ubrr, frame_cycles(s), work, N, r.got, r.wrong, r.frame, r.over, passed(r) ? 1 : 0);
  report(buf);
}

/**
 * @brief Set the baud rate and run one test.
 *
 * @param s The baud-rate setting.
 * @param work The work value passed to run_test().
 * @return The counts of the test.
 */
Result measure(const Setting &s, uint16_t work) {
  uart_init(s.ubrr, s.u2x);
  return run_test(work);
}

/**
 * @brief Run the whole sweep once and print it.
 *
 * For each baud rate: measure the fixed grid of work values, then search by bisection
 * for the largest work value that still gives no errors.
 */
void setup() {
  TIMSK0 = 0;                                   // no millis() interrupt stealing cycles
  report("\r\n# uart_sweep, N=10000, busy(n) = 5n-1 cycles\r\n"
         "kind,baud,mode,ubrr,cycles,work,sent,got,wrong,frame,over,pass\r\n");
  for (uint8_t i = 0; i < COUNT; i++) {
    const Setting &s = settings[i];
    // lo = largest WORK known to pass (-1: none), hi = smallest known to fail.
    // WORK = cycles/4 always fails: busy alone would then take more than a frame.
    int32_t lo = -1, hi = frame_cycles(s) / 4 + 1;
    for (uint8_t g = 0; g < GRID_COUNT; g++) {
      Result r = measure(s, grid[g]);
      print_row("grid", s, grid[g], r);
      if (passed(r)) { if (grid[g] > lo) lo = grid[g]; }
      else if (grid[g] < hi) hi = grid[g];
    }
    while (hi - lo > 1) {
      uint16_t mid = (lo + hi) / 2;
      Result r = measure(s, mid);
      print_row("bisect", s, mid, r);
      if (passed(r)) lo = mid; else hi = mid;
    }
    char buf[64];
    snprintf(buf, sizeof buf, "thresh,%u,%s,%u,%ld,%ld\r\n", frame_cycles(s), s.u2x ? "U2X" : "Normal",
             s.ubrr, (long)lo, (long)hi);
    report(buf);
  }
  report("# DONE (thresh: cycles,mode,ubrr,largest passing WORK,smallest failing WORK)\r\n");
}

/**
 * @brief Not used: the sweep runs once in setup().
 */
void loop() {}
