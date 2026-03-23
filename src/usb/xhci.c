#include "usb/xhci.h"

void xhci_attach(usb_controller_t *ctrl) {
  if (!ctrl) return;
  ctrl->ready = 1;
}
