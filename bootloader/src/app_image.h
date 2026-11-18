/*
 * SPDX-License-Identifier: MIT
 */

#ifndef UF2_APP_IMAGE_H
#define UF2_APP_IMAGE_H

#include <stdbool.h>
#include <stdint.h>
#include <zephyr/storage/flash_map.h>

#define APP_IMAGE_START PARTITION_ADDRESS(slot0_partition)
#define APP_IMAGE_SIZE  PARTITION_SIZE(slot0_partition)
#define APP_IMAGE_END   (APP_IMAGE_START + APP_IMAGE_SIZE)

/* Sanity-check slot0's vector table (initial SP and reset handler). */
bool app_image_valid(void);

/* Tear down bootloader state and branch to slot0. Does not return. */
FUNC_NORETURN void app_image_boot(void);

#endif
