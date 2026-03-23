#include "cpu/pic.h"

#include "cpu/ports.h"

enum {
  PIC1 = 0x20,
  PIC2 = 0xA0,
  PIC1_COMMAND = PIC1,
  PIC1_DATA = PIC1 + 1,
  PIC2_COMMAND = PIC2,
  PIC2_DATA = PIC2 + 1,
  ICW1_INIT = 0x10,
  ICW1_ICW4 = 0x01,
  ICW4_8086 = 0x01,
};

static void pic_remap(uint8_t offset1, uint8_t offset2) {
  uint8_t a1 = inb(PIC1_DATA);
  uint8_t a2 = inb(PIC2_DATA);

  outb(PIC1_COMMAND, (uint8_t)(ICW1_INIT | ICW1_ICW4));
  io_wait();
  outb(PIC2_COMMAND, (uint8_t)(ICW1_INIT | ICW1_ICW4));
  io_wait();

  outb(PIC1_DATA, offset1);
  io_wait();
  outb(PIC2_DATA, offset2);
  io_wait();

  outb(PIC1_DATA, 4);
  io_wait();
  outb(PIC2_DATA, 2);
  io_wait();

  outb(PIC1_DATA, ICW4_8086);
  io_wait();
  outb(PIC2_DATA, ICW4_8086);
  io_wait();

  outb(PIC1_DATA, a1);
  outb(PIC2_DATA, a2);
}

void pic_init(void) {
  pic_remap(0x20, 0x28);

  outb(PIC1_DATA, 0xFF);
  outb(PIC2_DATA, 0xFF);
}

void pic_send_eoi(uint8_t irq) {
  if (irq >= 8) {
    outb(PIC2_COMMAND, 0x20);
  }
  outb(PIC1_COMMAND, 0x20);
}

void pic_set_mask(uint8_t irq) {
  uint16_t port = (irq < 8) ? PIC1_DATA : PIC2_DATA;
  uint8_t bit = (uint8_t)(irq & 7);
  uint8_t value = inb(port);
  outb(port, (uint8_t)(value | (1u << bit)));
}

void pic_clear_mask(uint8_t irq) {
  uint16_t port = (irq < 8) ? PIC1_DATA : PIC2_DATA;
  uint8_t bit = (uint8_t)(irq & 7);
  uint8_t value = inb(port);
  outb(port, (uint8_t)(value & (uint8_t)~(1u << bit)));
}
