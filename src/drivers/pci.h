#pragma once

#include <stdint.h>

#define PCI_CLASS_SERIAL_BUS 0x0Cu
#define PCI_SUBCLASS_USB     0x03u

typedef struct {
  uint8_t  bus;
  uint8_t  dev;
  uint8_t  func;
  uint16_t vendor_id;
  uint16_t device_id;
  uint8_t  class_code;
  uint8_t  subclass;
  uint8_t  prog_if;
  uint8_t  revision;
} pci_device_t;

typedef int (*pci_visit_fn)(const pci_device_t *dev, void *ctx);

void     pci_init(void);
uint32_t pci_read32(uint8_t bus, uint8_t dev, uint8_t func, uint8_t off);
uint16_t pci_read16(uint8_t bus, uint8_t dev, uint8_t func, uint8_t off);
uint8_t  pci_read8(uint8_t bus, uint8_t dev, uint8_t func, uint8_t off);
void     pci_write32(uint8_t bus, uint8_t dev, uint8_t func, uint8_t off, uint32_t val);
void     pci_write16(uint8_t bus, uint8_t dev, uint8_t func, uint8_t off, uint16_t val);
void     pci_enable_busmaster(uint8_t bus, uint8_t dev, uint8_t func);
int      pci_enumerate(pci_visit_fn fn, void *ctx);
