/**
 * @file uart_demo.ino
 * @brief Oscilloscope demonstration of interrupts against polling, modes 0 to 3.
 * @author Harshal Narsaria (2024MT60695)
 *
 * @details
 * @verbatim
 * Oscilloscope demo: the cost of I/O handling, interrupts vs polling, on the ATmega328P UART.
 * Both 2M modes do the same per-byte work as the measured tests (uart_loopback.ino and
 * uart_lean.ino): read the byte, check FE / DOR / sequence, count it. Only how TX is fed differs.
 * Runs forever in blocks of 10,000 bytes, so the scope shows a live picture.
 *
 * Mode jumpers, read at reset (jumper to GND; change them, then press RESET):
 *   D5   D4   (D2/D3 not used: D3 on this board reads LOW with nothing connected)
 *   open open  0: 9600 baud, sends 'A' (0x41) every ~2 ms (1 ms idle) -> read one frame bit by bit
 *   open GND   1: interrupt-fed TX + polled RX, 1 Mbaud            -> CPU busy but keeps up
 *   GND  open  2: interrupt-fed TX + polled RX, 2 Mbaud            -> CPU saturated, bytes lost
 *   GND  GND   3: TX and RX both polled (no interrupts), 2 Mbaud   -> keeps up at full speed
 * Loopback jumper: pin 1 (TX) -> pin 0 (RX) for modes 1-3. Remove it while uploading.
 *
 * Scope pins:
 *   D1  (TX)  the UART frames
 *   D8  (PB0) high while the TX interrupt body runs (modes 1, 2)
 *   D9  (PB1) high while the CPU is idle, waiting for a received byte (modes 1, 2)
 *   D10 (PB2) toggles every time a byte is lost (DOR set); trigger on either edge
 *   D13 (PB5, on-board LED) on = the last block lost bytes
 * @endverbatim
 */

#include <avr/io.h>
#include <avr/interrupt.h>

/**
 * @brief Number of bytes sent in each test.
 */
#define N 10000

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
 * Stores the next byte of the counting pattern into UDR0 and disables itself after N bytes.
 * Pin D8 (PB0) is high while this body runs, so the oscilloscope shows its length.
 */
ISR(USART_UDRE_vect) {
  PORTB |= (1<<PB0);
  UDR0 = (uint8_t)sent_count;
  if (++sent_count >= N) UCSR0B &= ~(1 << UDRIE0);
  PORTB &= ~(1<<PB0);
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
 * @brief Send and receive one block of N bytes with interrupt-driven transmit (modes 1 and 2).
 *
 * The same receive loop as uart_loopback.ino, plus two debug pins.
 * D9 (PB1) is high while no byte is waiting (CPU idle).
 * D10 (PB2) toggles each time an overrun is seen (one lost byte).
 * A block ends once nothing has arrived for 2000 idle passes (about 3 ms).
 *
 * @return The counts of the block.
 */
Result interrupt_block(void) {
  Result r = {0,0,0,0};
  uint8_t expect = 0; uint32_t idle = 0;
  sent_count = 0;
  UCSR0B |= (1<<UDRIE0);
  while (r.got < N && idle < 2000UL) {
    if (UCSR0A & (1<<RXC0)) {
      PORTB &= ~(1<<PB1);
      uint8_t status = UCSR0A; uint8_t d = UDR0;
      if (status & (1<<FE0))  r.frame++;
      if (status & (1<<DOR0)) { r.over++; PINB = (1<<PB2); }   // writing 1 to PINB toggles
      if (d != expect) r.wrong++;
      expect = d + 1; r.got++; idle = 0;
    } else { PORTB |= (1<<PB1); idle++; }
  }
  UCSR0B &= ~(1<<UDRIE0);
  while (!(UCSR0A & (1<<TXC0)));
  return r;
}

/**
 * @brief Send and receive one block of N bytes with transmit and receive polled (mode 3).
 *
 * The same loop as uart_lean.ino, plus the D10 loss marker.
 * There is no idle pin here: its extra cycles would push the worst-case pass
 * above the 80-cycle frame.
 *
 * @return The counts of the block.
 */
Result polled_block(void) {
  Result r = {0,0,0,0};
  uint16_t sent = 0; uint8_t expect = 0; uint32_t idle = 0;
  while (r.got < N && idle < 2000UL) {
    uint8_t st = UCSR0A;
    if (sent < N && (st & (1<<UDRE0))) UDR0 = (uint8_t)sent++;
    if (st & (1<<RXC0)) {
      uint8_t d = UDR0;
      if (st & (1<<FE0))  r.frame++;
      if (st & (1<<DOR0)) { r.over++; PINB = (1<<PB2); }
      if (d != expect) r.wrong++;
      expect = d + 1; r.got++; idle = 0;
    } else idle++;
  }
  while (!(UCSR0A & (1<<TXC0)));
  return r;
}

/**
 * @brief Read the mode jumpers, show the mode on the LED, then run that mode for ever.
 *
 * The mode is read once from D5 and D4 (jumper to GND = 1).
 * The on-board LED blinks (mode + 1) times.
 * In modes 1 to 3 the LED is on if the last block lost any byte.
 */
void setup() {
  DDRB |= (1<<PB0) | (1<<PB1) | (1<<PB2) | (1<<PB5);
  PORTB &= ~((1<<PB0) | (1<<PB1) | (1<<PB2) | (1<<PB5));
  pinMode(4, INPUT_PULLUP);
  pinMode(5, INPUT_PULLUP);
  delay(5);
  uint8_t mode = (digitalRead(5) == LOW ? 2 : 0) | (digitalRead(4) == LOW ? 1 : 0);

  // Show the mode on the L LED at reset: (mode + 1) slow blinks, then a 1 s pause.
  // 1 blink = mode 0, 2 = mode 1, 3 = mode 2, 4 = mode 3. (The bootloader's quick flicker comes first.)
  delay(500);
  for (uint8_t i = 0; i <= mode; i++) {
    PORTB |= (1<<PB5);  delay(300);
    PORTB &= ~(1<<PB5); delay(300);
  }
  delay(1000);

  if (mode == 0) {
    uart_init(103, false);                      // 9615 baud
    for (;;) {
      while (!(UCSR0A & (1<<UDRE0)));
      UDR0 = 0x41;                              // sts 0x00C6: this store starts the frame
      delay(2);                                 // frame is 1.04 ms, so this leaves ~1 ms idle high
    }
  }
  TIMSK0 = 0;                                   // no millis() interrupt from here on
  if (mode == 1) uart_init(0, false);           // 1 Mbaud
  else           uart_init(0, true);            // 2 Mbaud (U2X)
  for (;;) {
    while (UCSR0A & (1<<RXC0)) (void)UDR0;
    Result r = (mode == 3) ? polled_block() : interrupt_block();
    if (r.got != N || r.over || r.wrong || r.frame) PORTB |= (1<<PB5);
    else PORTB &= ~(1<<PB5);
  }
}

/**
 * @brief Not used: setup() never returns.
 */
void loop() {}
