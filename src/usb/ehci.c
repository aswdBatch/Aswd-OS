#include "usb/ehci.h"

void ehci_attach(usb_controller_t *ctrl) {
  if (!ctrl) return;
  ctrl->ready = 1;
}
