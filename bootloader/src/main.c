/*
 * SPDX-License-Identifier: MIT
 *
 * UF2 bootloader entry point.
 *
 *   1. dbl_tap_should_enter_dfu() decides DFU vs chain-load.
 *   2. DFU: bring up USB MSC + ghost-FAT. The host drops a .uf2 onto
 *      the volume; ghostfat parses + dispatches to board_flash. When
 *      the host is done, board_flash_flush() triggers chain-load.
 *   3. Else: chain-load slot0 directly (SCB->VTOR + MSP swap).
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/usb/usbd.h>
#include <zephyr/usb/class/usbd_msc.h>

#include "board_flash.h"
#include "boot_jump.h"
#include "dbl_tap.h"
#include "ghostfat_disk.h"
#include "indicator.h"
#include "usbd_setup.h"

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

USBD_DEFINE_MSC_LUN(uf2, "UF2", "ZMK", "UF2Boot", "0.01");

static int run_dfu(void)
{
    int rc = uf2_disk_register();
    if (rc) {
        return rc;
    }
    if (!uf2_usbd_init_device()) {
        return -ENODEV;
    }
    indicator_set(INDICATOR_UNPLUGGED);
    LOG_INF("DFU ready -- waiting for USB host");
    return 0;
}

int main(void)
{
    LOG_INF("UF2 bootloader starting (board=" CONFIG_UF2_BOARD_ID ")");

    if (dbl_tap_should_enter_dfu()) {
        return run_dfu();
    }
    boot_jump_to_app(BOARD_FLASH_APP_START);
    return 0; /* unreachable */
}
