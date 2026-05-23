/* SPDX-License-Identifier: MIT */

#ifndef UF2_BOOT_JUMP_H
#define UF2_BOOT_JUMP_H

#include <stdint.h>

/*
 * Chain-load the application image starting at app_base. Does not
 * return on success.
 */
void boot_jump_to_app(uint32_t app_base);

#endif
