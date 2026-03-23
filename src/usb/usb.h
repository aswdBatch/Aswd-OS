#pragma once

#include <stdint.h>

typedef enum {
  USB_CTRL_NONE = 0,
  USB_CTRL_UHCI,
  USB_CTRL_OHCI,
  USB_CTRL_EHCI,
  USB_CTRL_XHCI,
} usb_controller_kind_t;

typedef struct {
  usb_controller_kind_t kind;
  uint8_t  bus;
  uint8_t  dev;
  uint8_t  func;
  uint16_t vendor_id;
  uint16_t device_id;
  uint32_t bar0;
  uint8_t  prog_if;
  uint8_t  ready;
} usb_controller_t;

typedef struct {
  int controller_count;
  int hid_keyboard_count;
  int hid_mouse_count;
  int active_pointer_source;
} usb_status_t;

void                    usb_init(void);
void                    usb_poll(void);
int                     usb_controller_count(void);
const usb_controller_t *usb_controller_at(int index);
const usb_status_t     *usb_status(void);
const char             *usb_controller_kind_name(usb_controller_kind_t kind);
