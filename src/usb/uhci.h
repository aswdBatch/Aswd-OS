#pragma once

#include "usb/usb.h"

void uhci_attach(usb_controller_t *ctrl);
void uhci_poll(usb_controller_t *ctrl);
