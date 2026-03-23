#include "usb/ohci.h"

void ohci_attach(usb_controller_t *ctrl) {
  if (!ctrl) return;
  ctrl->ready = 1;
}
