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

LOG_MODULE_REGISTER(uf2_disk, LOG_LEVEL_INF);

#define GHOSTFAT_DISK_NAME      "UF2"
#define GHOSTFAT_SECTOR_SIZE    512U

static int gf_init(struct disk_info *disk)
{
    ARG_UNUSED(disk);
    return 0;
}

/*
 * Soft eject. After the image is committed the host is usually still
 * flushing FAT/directory updates. Yanking USB then makes macOS say "Disk
 * Not Ejected Properly" and Windows pop write-error dialogs. Instead:
 *
 *   QUIET   keep serving the disk until the host has been idle for
 *           UF2_EJECT_QUIET_MS (every read/write restarts the timer)
 *   NOMEDIA report "medium not present"; the host sees it on its next
 *           TEST UNIT READY poll and unmounts, as for a pulled SD card.
 *           Needs CONFIG_USBD_MSC_MEDIUM_LOCK=n (zephyr/patches): if the
 *           host may lock the medium, macOS stops polling after mount.
 *   reboot  UF2_EJECT_ACK_MS after the host first sees NOMEDIA, or at
 *           UF2_EJECT_TIMEOUT_MS after commit if it never polls
 */
enum eject_state { EJECT_IDLE, EJECT_QUIET, EJECT_NOMEDIA, EJECT_ACKED };

static atomic_t eject_state = ATOMIC_INIT(EJECT_IDLE);
static int64_t eject_t0;

static void reboot_handler(struct k_work *work)
{
    ARG_UNUSED(work);
    LOG_INF("eject: rebooting (%s, %lld ms after commit)",
            atomic_get(&eject_state) == EJECT_ACKED ? "host acked" : "timeout",
            k_uptime_get() - eject_t0);
    sys_reboot(SYS_REBOOT_WARM);
}
static K_WORK_DELAYABLE_DEFINE(reboot_work, reboot_handler);

static void quiet_handler(struct k_work *work)
{
    ARG_UNUSED(work);
    if (atomic_cas(&eject_state, EJECT_QUIET, EJECT_NOMEDIA)) {
        LOG_INF("eject: host idle, reporting no media (+%lld ms)", k_uptime_get() - eject_t0);
    }
}
static K_WORK_DELAYABLE_DEFINE(quiet_work, quiet_handler);

static void host_activity(void)
{
    if (atomic_get(&eject_state) == EJECT_QUIET) {
        LOG_DBG("eject: host I/O while quiet (+%lld ms)", k_uptime_get() - eject_t0);
        k_work_reschedule(&quiet_work, K_MSEC(CONFIG_UF2_EJECT_QUIET_MS));
    }
}

void uf2_disk_request_eject(void)
{
    eject_t0 = k_uptime_get();
    atomic_set(&eject_state, EJECT_QUIET);
    k_work_reschedule(&quiet_work, K_MSEC(CONFIG_UF2_EJECT_QUIET_MS));
    k_work_reschedule(&reboot_work, K_MSEC(CONFIG_UF2_EJECT_TIMEOUT_MS));
}

void uf2_disk_cancel_eject(void)
{
    atomic_set(&eject_state, EJECT_IDLE);
    (void)k_work_cancel_delayable(&quiet_work);
    (void)k_work_cancel_delayable(&reboot_work);
}

static int gf_status(struct disk_info *disk)
{
    ARG_UNUSED(disk);

    switch (atomic_get(&eject_state)) {
    case EJECT_NOMEDIA:
        if (atomic_cas(&eject_state, EJECT_NOMEDIA, EJECT_ACKED)) {
            LOG_INF("eject: host saw no media (+%lld ms)", k_uptime_get() - eject_t0);
            k_work_reschedule(&reboot_work, K_MSEC(CONFIG_UF2_EJECT_ACK_MS));
        }
        return DISK_STATUS_NOMEDIA;
    case EJECT_ACKED:
        LOG_DBG("eject: status poll (+%lld ms)", k_uptime_get() - eject_t0);
        return DISK_STATUS_NOMEDIA;
    default:
        return DISK_STATUS_OK;
    }
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
