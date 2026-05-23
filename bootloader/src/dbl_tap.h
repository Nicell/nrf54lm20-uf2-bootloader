/* SPDX-License-Identifier: MIT */

#ifndef UF2_DBL_TAP_H
#define UF2_DBL_TAP_H

#include <stdbool.h>

/* Magic values written to the noinit RAM word that survives warm resets.
 * Compatible with Adafruit/tinyuf2 so external tooling that writes these
 * values (e.g. from an application "&bootloader" behavior) behaves the
 * same on this bootloader as on existing nRF52 UF2 bootloaders.
 */
#define DBL_TAP_MAGIC            0xf01669efU  /* enter DFU on next boot */
#define DBL_TAP_MAGIC_QUICK_BOOT 0xf02669efU  /* skip the 500 ms window */

/* Returns true if the bootloader should enter DFU mode (USB MSC up).
 * Returns false if it should chain-load slot0. Side effect: also arms
 * the double-tap window when in the "first reset" case.
 */
bool dbl_tap_should_enter_dfu(void);

#endif
