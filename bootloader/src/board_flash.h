/*
 * SPDX-License-Identifier: MIT
 *
 * slot0 access for the UF2 engine. Addresses are absolute (as in UF2
 * targetAddr); everything outside slot0 is rejected.
 */

#ifndef UF2_BOARD_FLASH_H
#define UF2_BOARD_FLASH_H

#include <stdbool.h>
#include <stdint.h>

#include "app_image.h"

#define BOARD_FLASH_APP_START APP_IMAGE_START

uint32_t board_flash_size(void);
uint32_t board_flash_write_align(void);
bool board_flash_in_slot(uint32_t addr, uint32_t len);
void board_flash_read(uint32_t addr, void *buffer, uint32_t len);
int board_flash_write(uint32_t addr, const void *data, uint32_t len);

#endif /* UF2_BOARD_FLASH_H */
