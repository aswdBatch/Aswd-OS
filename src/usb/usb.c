#include "usb/usb.h"

#include "drivers/pci.h"
#include "usb/ehci.h"
#include "usb/hid.h"
#include "usb/ohci.h"
#include "usb/uhci.h"
#include "usb/xhci.h"

#define USB_MAX_CONTROLLERS 16
#define USB_MAX_DEVICES     16

static usb_controller_t g_controllers[USB_MAX_CONTROLLERS];
static usb_device_t g_devices[USB_MAX_DEVICES];
static usb_status_t g_status;

static usb_controller_kind_t usb_kind_from_prog_if(uint8_t prog_if) {
  switch (prog_if) {
    case 0x00: return USB_CTRL_UHCI;
    case 0x10: return USB_CTRL_OHCI;
    case 0x20: return USB_CTRL_EHCI;
    case 0x30: return USB_CTRL_XHCI;
    default:   return USB_CTRL_NONE;
  }
}

const char *usb_controller_kind_name(usb_controller_kind_t kind) {
  switch (kind) {
    case USB_CTRL_UHCI: return "UHCI";
    case USB_CTRL_OHCI: return "OHCI";
    case USB_CTRL_EHCI: return "EHCI";
    case USB_CTRL_XHCI: return "xHCI";
    default:            return "Unknown";
  }
}

static void usb_attach_controller(usb_controller_t *ctrl) {
  if (!ctrl) return;

  switch (ctrl->kind) {
    case USB_CTRL_UHCI: uhci_attach(ctrl); break;
    case USB_CTRL_OHCI: ohci_attach(ctrl); break;
    case USB_CTRL_EHCI: ehci_attach(ctrl); break;
    case USB_CTRL_XHCI: xhci_attach(ctrl); break;
    default: break;
  }
}

static int usb_collect_controller(const pci_device_t *dev, void *ctx) {
  int *count = (int *)ctx;
  usb_controller_t *ctrl;
  if (!dev || !count) return 0;
  if (dev->class_code != PCI_CLASS_SERIAL_BUS ||
      dev->subclass != PCI_SUBCLASS_USB) {
    return 0;
  }
  if (*count >= USB_MAX_CONTROLLERS) {
    return 1;
  }

  ctrl = &g_controllers[*count];
  ctrl->kind = usb_kind_from_prog_if(dev->prog_if);
  ctrl->bus = dev->bus;
  ctrl->dev = dev->dev;
  ctrl->func = dev->func;
  ctrl->vendor_id = dev->vendor_id;
  ctrl->device_id = dev->device_id;
  ctrl->bar0 = pci_read32(dev->bus, dev->dev, dev->func, 0x10) & 0xFFFFFFF0u;
  ctrl->prog_if = dev->prog_if;
  ctrl->ready = 0;
  ctrl->supports_control = 0;
  ctrl->supports_interrupt = 0;
  ctrl->supports_bulk = 0;
  usb_attach_controller(ctrl);
  (*count)++;
  return 0;
}

void usb_register_device(const usb_device_t *device) {
  if (!device) return;
  if (g_status.device_count >= USB_MAX_DEVICES) return;
  g_devices[g_status.device_count] = *device;
  g_status.device_count++;
}

void usb_init(void) {
  int count = 0;
  for (int i = 0; i < USB_MAX_CONTROLLERS; i++) {
    g_controllers[i].kind = USB_CTRL_NONE;
    g_controllers[i].ready = 0;
    g_controllers[i].supports_control = 0;
    g_controllers[i].supports_interrupt = 0;
    g_controllers[i].supports_bulk = 0;
  }
  for (int i = 0; i < USB_MAX_DEVICES; i++) {
    g_devices[i].vendor_id = 0;
    g_devices[i].product_id = 0;
  }
  g_status.controller_count = 0;
  g_status.device_count = 0;
  g_status.hid_keyboard_count = 0;
  g_status.hid_mouse_count = 0;
  g_status.active_pointer_source = 0;

  pci_enumerate(usb_collect_controller, &count);
  g_status.controller_count = count;
  usb_hid_init();
}

int usb_controller_count(void) {
  return g_status.controller_count;
}

const usb_controller_t *usb_controller_at(int index) {
  if (index < 0 || index >= g_status.controller_count) return 0;
  return &g_controllers[index];
}

int usb_device_count(void) {
  return g_status.device_count;
}

const usb_device_t *usb_device_at(int index) {
  if (index < 0 || index >= g_status.device_count) return 0;
  return &g_devices[index];
}

const usb_status_t *usb_status(void) {
  return &g_status;
}

void usb_poll(void) {
  int i;
  for (i = 0; i < g_status.controller_count; i++) {
    usb_controller_t *ctrl = &g_controllers[i];
    if (ctrl->kind == USB_CTRL_UHCI) {
      uhci_poll(ctrl);
    }
  }
}
