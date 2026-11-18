/*
 * SPDX-License-Identifier: MIT
 *
 * UF2 bootloader entry point.
 *
 *   1. boot_entry_check() decides DFU vs application. USB is not touched
 *      on the normal path, so booting the app costs only Zephyr init.
 *   2. DFU: bring up USB MSC + ghost-FAT. The host drops a .uf2 onto the
 *      volume; uf2_writer validates and writes it, commits the vector
 *      table last, then reboots. The next boot takes path 3.
 *   3. Otherwise chain-load slot0.
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/usb/usbd.h>
#include <zephyr/usb/class/usbd_msc.h>

#include "app_image.h"
#include "boot_entry.h"
#include "ghostfat_disk.h"
#include "uf2_status.h"
#include "usbd_setup.h"

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

USBD_DEFINE_MSC_LUN(uf2, "UF2", "Lily", "UF2 Bootloader", "0.2");

static int run_dfu(void)
{
	int rc = uf2_disk_register();

	if (rc) {
		return rc;
	}
	if (!uf2_usbd_init_device()) {
		return -ENODEV;
	}
	uf2_status_set_state(UF2_STATE_WAITING);
	LOG_INF("DFU ready, waiting for USB host");
	return 0;
}

int main(void)
{
	enum uf2_entry_reason reason;

	LOG_INF("UF2 bootloader (" CONFIG_UF2_BOARD_ID ")");

	reason = boot_entry_check();
	if (reason == UF2_ENTRY_NONE) {
		app_image_boot();
	}

	LOG_INF("entering DFU: %s", uf2_entry_reason_str(reason));
	uf2_status_set_reason(reason);
	return run_dfu();
}
