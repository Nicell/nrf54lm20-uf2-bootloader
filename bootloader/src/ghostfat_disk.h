/* SPDX-License-Identifier: Apache-2.0 */

#ifndef UF2_GHOSTFAT_DISK_H
#define UF2_GHOSTFAT_DISK_H

/*
 * Initialize the ghost-FAT engine and register the synthetic disk
 * with Zephyr's disk subsystem so the USB MSC class can serve it.
 * Returns 0 on success or a negative errno.
 */
int uf2_disk_register(void);

/*
 * Signal that the host has finished writing the UF2 file; schedules a
 * warm reset so the freshly-flashed application boots.
 */
void uf2_disk_on_complete(void);

#endif
