#include "drivers/serial.h"

#include <stdint.h>

#include "cpu/pic.h"
#include "cpu/ports.h"

enum { COM1 = 0x3F8 };

static volatile char g_rx[256];
static volatile uint8_t g_rx_head = 0;
static volatile uint8_t g_rx_tail = 0;
static int g_enabled = 0;

static void rx_push(char c) {
  uint8_t next = (uint8_t)(g_rx_head + 1);
  if (next == g_rx_tail) {
    return;
  }
  g_rx[g_rx_head] = c;
  g_rx_head = next;
}

int serial_try_getchar(char *out) {
  if (!g_enabled) {
    return 0;
  }
  if (g_rx_tail == g_rx_head) {
    return 0;
  }
  *out = g_rx[g_rx_tail];
  g_rx_tail = (uint8_t)(g_rx_tail + 1);
  return 1;
}

static int tx_ready(void) {
  return (inb((uint16_t)(COM1 + 5)) & 0x20u) != 0;
}

static int rx_ready(void) {
  return (inb((uint16_t)(COM1 + 5)) & 0x01u) != 0;
}

void serial_write_char(char c) {
  if (!g_enabled) {
    return;
  }
  while (!tx_ready()) {
  }
  outb(COM1, (uint8_t)c);
}

void serial_write(const char *s) {
  for (int i = 0; s && s[i]; i++) {
    serial_write_char(s[i]);
  }
}

int serial_is_enabled(void) {
  return g_enabled;
}

void serial_init(void) {
  g_rx_head = 0;
  g_rx_tail = 0;

  /* Scratch-register presence test.
     A real 16550A echoes whatever is written to COM1+7.
     Non-existent hardware returns 0xFF regardless.        */
  outb((uint16_t)(COM1 + 7), 0xAA);
  if (inb((uint16_t)(COM1 + 7)) != 0xAA) {
    return;   /* No UART — leave g_enabled=0, don't touch IRQ4 */
  }

  outb((uint16_t)(COM1 + 1), 0x00);
  outb((uint16_t)(COM1 + 3), 0x80);
  outb((uint16_t)(COM1 + 0), 0x01);
  outb((uint16_t)(COM1 + 1), 0x00);
  outb((uint16_t)(COM1 + 3), 0x03);
  outb((uint16_t)(COM1 + 2), 0xC7);
  outb((uint16_t)(COM1 + 4), 0x0B);
  outb((uint16_t)(COM1 + 1), 0x01);

  g_enabled = 1;
  pic_clear_mask(4);

  for (int i = 0; i < 16 && rx_ready(); i++) {
    (void)inb(COM1);
  }
}

void serial_irq_handler(void) {
  if (!g_enabled) {
    pic_send_eoi(4);
    return;
  }

  while (rx_ready()) {
    char c = (char)inb(COM1);
    rx_push(c);
  }

  pic_send_eoi(4);
}

