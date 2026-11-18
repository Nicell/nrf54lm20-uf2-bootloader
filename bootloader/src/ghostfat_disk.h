/* SPDX-License-Identifier: MIT */

#ifndef UF2_GHOSTFAT_DISK_H
#define UF2_GHOSTFAT_DISK_H

/*
 * Initialize the ghost-FAT engine and register the synthetic disk
 * with Zephyr's disk subsystem so the USB MSC class can serve it.
 * Returns 0 on success or a negative errno.
 */
int uf2_disk_register(void);

/* Image committed: reboot once the host has finished with the disk. */
void uf2_disk_request_reboot(void);

/* A new transfer started before the reboot: keep serving the disk. */
void uf2_disk_cancel_reboot(void);

/*
 * Signal that the host has finished writing the UF2 file; schedules a
 * warm reset so the freshly-flashed application boots.
 */

#endif
