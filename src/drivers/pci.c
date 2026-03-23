#include "drivers/pci.h"

#include "cpu/ports.h"

#define PCI_CONFIG_ADDR 0x0CF8u
#define PCI_CONFIG_DATA 0x0CFCu

void pci_init(void) {
}

uint32_t pci_read32(uint8_t bus, uint8_t dev, uint8_t func, uint8_t off) {
  uint32_t addr = 0x80000000u |
      ((uint32_t)bus << 16) |
      ((uint32_t)dev << 11) |
      ((uint32_t)func << 8) |
      (off & 0xFCu);
  outl(PCI_CONFIG_ADDR, addr);
  return inl(PCI_CONFIG_DATA);
}

uint16_t pci_read16(uint8_t bus, uint8_t dev, uint8_t func, uint8_t off) {
  uint32_t value = pci_read32(bus, dev, func, off);
  return (uint16_t)((value >> ((off & 2u) * 8u)) & 0xFFFFu);
}

uint8_t pci_read8(uint8_t bus, uint8_t dev, uint8_t func, uint8_t off) {
  uint32_t value = pci_read32(bus, dev, func, off);
  return (uint8_t)((value >> ((off & 3u) * 8u)) & 0xFFu);
}

void pci_write32(uint8_t bus, uint8_t dev, uint8_t func, uint8_t off, uint32_t val) {
  uint32_t addr = 0x80000000u |
      ((uint32_t)bus << 16) |
      ((uint32_t)dev << 11) |
      ((uint32_t)func << 8) |
      (off & 0xFCu);
  outl(PCI_CONFIG_ADDR, addr);
  outl(PCI_CONFIG_DATA, val);
}

void pci_write16(uint8_t bus, uint8_t dev, uint8_t func, uint8_t off, uint16_t val) {
  uint32_t cur = pci_read32(bus, dev, func, off);
  uint8_t shift = (uint8_t)((off & 2u) * 8u);
  cur &= ~(0xFFFFu << shift);
  cur |= ((uint32_t)val << shift);
  pci_write32(bus, dev, func, off, cur);
}

/* Enable PCI bus mastering (bit 2 of command register at offset 0x04) */
void pci_enable_busmaster(uint8_t bus, uint8_t dev, uint8_t func) {
  uint16_t cmd = pci_read16(bus, dev, func, 0x04);
  if (!(cmd & 0x04u)) {
    pci_write16(bus, dev, func, 0x04, (uint16_t)(cmd | 0x04u | 0x02u | 0x01u));
  }
}

static int pci_visit_slot(uint8_t bus, uint8_t dev, uint8_t func,
                          pci_visit_fn fn, void *ctx) {
  pci_device_t desc;
  uint32_t id = pci_read32(bus, dev, func, 0x00);
  if ((id & 0xFFFFu) == 0xFFFFu) {
    return 0;
  }

  desc.bus = bus;
  desc.dev = dev;
  desc.func = func;
  desc.vendor_id = (uint16_t)(id & 0xFFFFu);
  desc.device_id = (uint16_t)((id >> 16) & 0xFFFFu);
  desc.revision = pci_read8(bus, dev, func, 0x08);
  desc.prog_if = pci_read8(bus, dev, func, 0x09);
  desc.subclass = pci_read8(bus, dev, func, 0x0A);
  desc.class_code = pci_read8(bus, dev, func, 0x0B);
  return fn ? fn(&desc, ctx) : 0;
}

int pci_enumerate(pci_visit_fn fn, void *ctx) {
  for (uint16_t bus = 0; bus < 256; bus++) {
    for (uint8_t dev = 0; dev < 32; dev++) {
      uint8_t header_type = pci_read8((uint8_t)bus, dev, 0, 0x0E);
      uint8_t funcs = (header_type & 0x80u) ? 8u : 1u;
      for (uint8_t func = 0; func < funcs; func++) {
        int result = pci_visit_slot((uint8_t)bus, dev, func, fn, ctx);
        if (result != 0) {
          return result;
        }
      }
    }
  }
  return 0;
}
