/* SPDX-License-Identifier: Apache-2.0 */

#ifndef UF2_BOARD_FLASH_H
#define UF2_BOARD_FLASH_H

#include <stdint.h>
#include <stdbool.h>

#include <zephyr/storage/flash_map.h>

/* Base address that UF2 target addresses are measured against. */
#define BOARD_FLASH_ADDR_ZERO       0

/* First byte the bootloader may write — start of slot0_partition. */
#define BOARD_FLASH_APP_START       PARTITION_OFFSET(slot0_partition)

/* Size of the flash region the bootloader exposes as CURRENT.UF2. */
uint32_t board_flash_size(void);

/* Read len bytes from absolute flash addr into buffer. */
void board_flash_read(uint32_t addr, void *buffer, uint32_t len);

/*
 * Write len bytes to absolute flash addr. UF2 payloads are 256 B and
 * addresses are 256 B aligned. RRAM is write-anywhere, so no explicit
 * erase is needed.
 */
bool board_flash_write(uint32_t addr, void const *data, uint32_t len);

/* Flush any cached state to flash. */
void board_flash_flush(void);

#endif /* UF2_BOARD_FLASH_H */
