/* SPDX-License-Identifier: MIT */

#ifndef UF2_USBD_SETUP_H
#define UF2_USBD_SETUP_H

#include <zephyr/usb/usbd.h>

struct usbd_context *uf2_usbd_init_device(void);

#endif
