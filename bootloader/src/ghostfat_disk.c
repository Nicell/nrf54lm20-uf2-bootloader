/*
 * SPDX-License-Identifier: MIT
 *
 * Wraps the ghost-FAT block engine in a Zephyr struct disk_info so
 * the USB MSC class can serve it.
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/disk.h>
#include <zephyr/logging/log.h>

#include "uf2.h"
#include "ghostfat_disk.h"
#include "uf2_writer.h"

LOG_MODULE_REGISTER(uf2_disk, LOG_LEVEL_INF);

#define GHOSTFAT_DISK_NAME      "UF2"
#define GHOSTFAT_SECTOR_SIZE    512U

static int gf_init(struct disk_info *disk)
{
    ARG_UNUSED(disk);
    return 0;
}

static int gf_status(struct disk_info *disk)
{
    ARG_UNUSED(disk);
    return DISK_STATUS_OK;
}

static int gf_read(struct disk_info *disk, uint8_t *buf,
                   uint32_t lba, uint32_t cnt)
{
    ARG_UNUSED(disk);
    while (cnt--) {
        uf2_read_block(lba++, buf);
        buf += GHOSTFAT_SECTOR_SIZE;
    }
    return 0;
}

static int gf_write(struct disk_info *disk, const uint8_t *buf,
                    uint32_t lba, uint32_t cnt)
{
    ARG_UNUSED(disk);
    while (cnt--) {
        uf2_writer_sector((const UF2_Block *)buf);
        lba++;
        buf += GHOSTFAT_SECTOR_SIZE;
    }
    return 0;
}

static int gf_erase(struct disk_info *disk, uint32_t start_sector,
                    uint32_t num_sector)
{
    ARG_UNUSED(disk);
    ARG_UNUSED(start_sector);
    ARG_UNUSED(num_sector);
    return 0;
}

static int gf_ioctl(struct disk_info *disk, uint8_t cmd, void *buff)
{
    ARG_UNUSED(disk);

    switch (cmd) {
    case DISK_IOCTL_CTRL_SYNC:
    case DISK_IOCTL_CTRL_INIT:
    case DISK_IOCTL_CTRL_DEINIT:
        return 0;
    case DISK_IOCTL_GET_SECTOR_COUNT:
        *(uint32_t *)buff = CFG_UF2_NUM_BLOCKS;
        return 0;
    case DISK_IOCTL_GET_SECTOR_SIZE:
        *(uint32_t *)buff = GHOSTFAT_SECTOR_SIZE;
        return 0;
    case DISK_IOCTL_GET_ERASE_BLOCK_SZ:
        *(uint32_t *)buff = 1U;
        return 0;
    default:
        return -EINVAL;
    }
}

static const struct disk_operations gf_ops = {
    .init   = gf_init,
    .status = gf_status,
    .read   = gf_read,
    .write  = gf_write,
    .erase  = gf_erase,
    .ioctl  = gf_ioctl,
};

static struct disk_info gf_disk = {
    .name = GHOSTFAT_DISK_NAME,
    .ops  = &gf_ops,
};

int uf2_disk_register(void)
{
    uf2_init();
    int rc = disk_access_register(&gf_disk);
    if (rc) {
        LOG_ERR("disk_access_register failed: %d", rc);
    }
    return rc;
}
