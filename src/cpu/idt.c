#include "cpu/idt.h"

#include <stdint.h>

#include "cpu/pic.h"
#include "cpu/ports.h"

typedef struct __attribute__((packed)) {
  uint16_t base_low;
  uint16_t sel;
  uint8_t always0;
  uint8_t flags;
  uint16_t base_high;
} idt_entry_t;

typedef struct __attribute__((packed)) {
  uint16_t limit;
  uint32_t base;
} idt_ptr_t;

extern void isr0_stub(void);
extern void isr1_stub(void);
extern void isr2_stub(void);
extern void isr3_stub(void);
extern void isr4_stub(void);
extern void isr5_stub(void);
extern void isr6_stub(void);
extern void isr7_stub(void);
extern void isr8_stub(void);
extern void isr9_stub(void);
extern void isr10_stub(void);
extern void isr11_stub(void);
extern void isr12_stub(void);
extern void isr13_stub(void);
extern void isr14_stub(void);
extern void irq0_stub(void);
extern void irq1_stub(void);
extern void irq4_stub(void);
extern void irq12_stub(void);
extern void spurious_irq_stub(void);

static idt_entry_t idt_entries[256];
static idt_ptr_t idt_ptr;

static void idt_set_gate(uint8_t num, uint32_t base, uint16_t sel, uint8_t flags) {
  idt_entries[num].base_low = (uint16_t)(base & 0xFFFFu);
  idt_entries[num].base_high = (uint16_t)((base >> 16) & 0xFFFFu);
  idt_entries[num].sel = sel;
  idt_entries[num].always0 = 0;
  idt_entries[num].flags = flags;
}

static void idt_clear(void) {
  for (uint16_t i = 0; i < 256; i++) {
    idt_entries[i].base_low = 0;
    idt_entries[i].base_high = 0;
    idt_entries[i].sel = 0;
    idt_entries[i].always0 = 0;
    idt_entries[i].flags = 0;
  }
}

void idt_init(void) {
  cpu_cli();

  pic_init();

  idt_clear();
  idt_ptr.limit = (uint16_t)(sizeof(idt_entries) - 1);
  idt_ptr.base = (uint32_t)&idt_entries[0];

  /* CPU exception handlers (vectors 0-13) */
  idt_set_gate(0,  (uint32_t)isr0_stub,  0x08, 0x8Eu);
  idt_set_gate(1,  (uint32_t)isr1_stub,  0x08, 0x8Eu);
  idt_set_gate(2,  (uint32_t)isr2_stub,  0x08, 0x8Eu);
  idt_set_gate(3,  (uint32_t)isr3_stub,  0x08, 0x8Eu);
  idt_set_gate(4,  (uint32_t)isr4_stub,  0x08, 0x8Eu);
  idt_set_gate(5,  (uint32_t)isr5_stub,  0x08, 0x8Eu);
  idt_set_gate(6,  (uint32_t)isr6_stub,  0x08, 0x8Eu);
  idt_set_gate(7,  (uint32_t)isr7_stub,  0x08, 0x8Eu);
  idt_set_gate(8,  (uint32_t)isr8_stub,  0x08, 0x8Eu);
  idt_set_gate(9,  (uint32_t)isr9_stub,  0x08, 0x8Eu);
  idt_set_gate(10, (uint32_t)isr10_stub, 0x08, 0x8Eu);
  idt_set_gate(11, (uint32_t)isr11_stub, 0x08, 0x8Eu);
  idt_set_gate(12, (uint32_t)isr12_stub, 0x08, 0x8Eu);
  idt_set_gate(13, (uint32_t)isr13_stub, 0x08, 0x8Eu);
  idt_set_gate(14, (uint32_t)isr14_stub, 0x08, 0x8Eu);

  /* Spurious IRQ handler — catches any unhandled PIC IRQ (0x20-0x2F).
     Real hardware generates spurious IRQ7 during PS/2 init; without a
     handler the CPU triple-faults on the zero/not-present IDT entry. */
  for (uint8_t v = 0x20; v <= 0x2F; v++) {
    idt_set_gate(v, (uint32_t)spurious_irq_stub, 0x08, 0x8Eu);
  }
  /* Override with real handlers */
  idt_set_gate(0x20, (uint32_t)irq0_stub, 0x08, 0x8Eu);
  idt_set_gate(0x21, (uint32_t)irq1_stub, 0x08, 0x8Eu);
  idt_set_gate(0x24, (uint32_t)irq4_stub, 0x08, 0x8Eu);
  idt_set_gate(0x2C, (uint32_t)irq12_stub, 0x08, 0x8Eu);

  __asm__ volatile("lidt %0" : : "m"(idt_ptr));
}
