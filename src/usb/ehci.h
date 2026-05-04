#pragma once

#include "usb/usb.h"

void ehci_attach(usb_controller_t *ctrl);
void ehci_poll(usb_controller_t *ctrl);
