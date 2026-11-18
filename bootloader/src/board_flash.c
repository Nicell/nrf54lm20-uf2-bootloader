/*
 * SPDX-License-Identifier: MIT
 *
 * nRF54LM20 RRAM needs no erase: a write simply replaces the bytes. Writes
 * go through the RRAMC write buffer (CONFIG_NRF_RRAM_WRITE_BUFFER_SIZE).
 */

#include <string.h>

#include <zephyr/cache.h>
#include <zephyr/logging/log.h>
#include <zephyr/storage/flash_map.h>

#include "board_flash.h"

LOG_MODULE_REGISTER(board_flash, LOG_LEVEL_INF);

static const struct flash_area *slot0_fa;

static int slot0_open(void)
{
	if (slot0_fa) {
		return 0;
	}
	int rc = flash_area_open(PARTITION_ID(slot0_partition), &slot0_fa);

	if (rc) {
		LOG_ERR("flash_area_open(slot0): %d", rc);
	}
	return rc;
}

uint32_t board_flash_size(void)
{
	return APP_IMAGE_SIZE;
}

uint32_t board_flash_write_align(void)
{
	return slot0_open() ? 16U : flash_area_align(slot0_fa);
}

bool board_flash_in_slot(uint32_t addr, uint32_t len)
{
	return addr >= APP_IMAGE_START && len <= APP_IMAGE_SIZE &&
	       addr - APP_IMAGE_START <= APP_IMAGE_SIZE - len;
}

void board_flash_read(uint32_t addr, void *buffer, uint32_t len)
{
	if (!board_flash_in_slot(addr, len)) {
		memset(buffer, 0xFF, len);
		return;
	}
	memcpy(buffer, (const void *)(uintptr_t)addr, len);
}

int board_flash_write(uint32_t addr, const void *data, uint32_t len)
{
	int rc = slot0_open();

	if (rc) {
		return rc;
	}
	if (!board_flash_in_slot(addr, len)) {
		return -EINVAL;
	}
	rc = flash_area_write(slot0_fa, addr - APP_IMAGE_START, data, len);
	if (rc) {
		LOG_ERR("write 0x%08x+%u: %d", addr, len, rc);
		return rc;
	}
	/* Reads below go through the CPU cache; drop any stale lines. */
	(void)sys_cache_data_invd_range((void *)(uintptr_t)addr, len);
	return 0;
}
