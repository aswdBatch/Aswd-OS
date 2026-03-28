#include "usb/ehci.h"

void ehci_attach(usb_controller_t *ctrl) {
  if (!ctrl) return;
  ctrl->ready = 0;
  ctrl->supports_control = 0;
  ctrl->supports_interrupt = 0;
  ctrl->supports_bulk = 0;
}
