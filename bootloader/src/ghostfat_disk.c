/*
 * SPDX-License-Identifier: MIT
 *
 * Wraps the ghost-FAT block engine in a Zephyr struct disk_info so
 * the USB MSC class can serve it.
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/disk.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/reboot.h>

#include "uf2.h"
#include "ghostfat_disk.h"
#include "uf2_writer.h"
#include "uf2_status.h"

LOG_MODULE_REGISTER(uf2_disk, LOG_LEVEL_INF);

#define GHOSTFAT_DISK_NAME      "UF2"
#define GHOSTFAT_SECTOR_SIZE    512U

static int gf_init(struct disk_info *disk)
{
    ARG_UNUSED(disk);
    return 0;
}

/*
 * Reboot only once the host has finished. The last UF2 block arrives
 * before the host has written the file's directory entry and FAT, and
 * before it has even received the status for that last WRITE. Vanishing
 * then makes Windows report the copy as failed (0x800701B1, "device does
 * not exist"); see microsoft/uf2-samdx1#11 and adafruit/tinyuf2#375.
 *
 * So after commit, keep serving the disk and reboot once the host has
 * issued no reads or writes for UF2_REBOOT_IDLE_MS (each one restarts
 * the timer), or UF2_REBOOT_MAX_MS after commit if it never goes idle.
 *
 * macOS still shows "Disk Not Ejected Properly": it warns on any unmount
 * the user did not request, and no device-side signal avoids that.
 */
static atomic_t reboot_pending;
static int64_t commit_ms;

static void reboot_handler(struct k_work *work)
{
    ARG_UNUSED(work);
    LOG_INF("rebooting %lld ms after commit", k_uptime_get() - commit_ms);
    uf2_status_set_state(UF2_STATE_REBOOTING);
    sys_reboot(SYS_REBOOT_WARM);
}
static K_WORK_DELAYABLE_DEFINE(idle_work, reboot_handler);
static K_WORK_DELAYABLE_DEFINE(max_work, reboot_handler);

static void host_activity(void)
{
    if (atomic_get(&reboot_pending)) {
        k_work_reschedule(&idle_work, K_MSEC(CONFIG_UF2_REBOOT_IDLE_MS));
    }
}

void uf2_disk_request_reboot(void)
{
    commit_ms = k_uptime_get();
    atomic_set(&reboot_pending, 1);
    k_work_reschedule(&idle_work, K_MSEC(CONFIG_UF2_REBOOT_IDLE_MS));
    k_work_reschedule(&max_work, K_MSEC(CONFIG_UF2_REBOOT_MAX_MS));
}

void uf2_disk_cancel_reboot(void)
{
    atomic_set(&reboot_pending, 0);
    (void)k_work_cancel_delayable(&idle_work);
    (void)k_work_cancel_delayable(&max_work);
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
    host_activity();
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
    host_activity();
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
