/*
 * SPDX-License-Identifier: MIT
 *
 * Flash backend for the ghost-FAT UF2 engine. nRF54LM20 has RRAM, so
 * writes do not need a pre-erase. The host may rewrite the same UF2
 * block harmlessly if a drag is interrupted.
 */

#include <string.h>

#include <zephyr/logging/log.h>
#include <zephyr/storage/flash_map.h>

#include "board_flash.h"
#include "ghostfat_disk.h"
#include "indicator.h"

LOG_MODULE_REGISTER(board_flash, LOG_LEVEL_INF);

#define SLOT0_ID    PARTITION_ID(slot0_partition)
#define SLOT0_OFF   PARTITION_OFFSET(slot0_partition)
#define SLOT0_SIZE  PARTITION_SIZE(slot0_partition)

static const struct flash_area *slot0_fa;

/* Open slot0 lazily — at SYS_INIT the RRAM driver isn't ready yet. */
static int slot0_open(void)
{
    if (slot0_fa) {
        return 0;
    }
    int rc = flash_area_open(SLOT0_ID, &slot0_fa);
    if (rc) {
        LOG_ERR("flash_area_open(slot0) failed: %d", rc);
        return rc;
    }
    LOG_INF("slot0: 0x%lx..0x%lx (%u bytes) on %s",
            (unsigned long)slot0_fa->fa_off,
            (unsigned long)(slot0_fa->fa_off + slot0_fa->fa_size),
            (unsigned int)slot0_fa->fa_size,
            slot0_fa->fa_dev->name);
    return 0;
}

uint32_t board_flash_size(void)
{
    return SLOT0_SIZE;
}

static int32_t to_slot_off(uint32_t addr, uint32_t len)
{
    if (addr < SLOT0_OFF || (addr + len) > (SLOT0_OFF + SLOT0_SIZE)) {
        return -1;
    }
    return (int32_t)(addr - SLOT0_OFF);
}

void board_flash_read(uint32_t addr, void *buffer, uint32_t len)
{
    /* RRAM is memory-mapped; direct memcpy works even before
     * slot0_open() (e.g. during app_present() at boot). */
    if (to_slot_off(addr, len) < 0) {
        memset(buffer, 0, len);
        return;
    }
    memcpy(buffer, (const void *)(uintptr_t)addr, len);
}

bool board_flash_write(uint32_t addr, void const *data, uint32_t len)
{
    indicator_set(INDICATOR_WRITING);

    if (slot0_open()) {
        return false;
    }
    int32_t off = to_slot_off(addr, len);
    if (off < 0) {
        LOG_WRN("write out of range addr=0x%08x len=%u", addr, len);
        return false;
    }
    int rc = flash_area_write(slot0_fa, off, data, len);
    if (rc) {
        LOG_WRN("flash_area_write off=0x%x len=%u rc=%d", (unsigned)off, len, rc);
        return false;
    }
    return true;
}

void board_flash_flush(void)
{
    /* RRAM writes commit synchronously; this is just the "host has
     * finished writing every UF2 block" trigger. */
    indicator_set(INDICATOR_FINISHED);
    uf2_disk_on_complete();
}
