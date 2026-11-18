/*
 * SPDX-License-Identifier: MIT
 *
 * Round-trip test: print a few lines from slot0, then set boot mode
 * BOOTLOADER and warm-reboot, exactly as ZMK's &bootloader does. The
 * UF2 drive should come back.
 */

#include <zephyr/kernel.h>
#include <zephyr/retention/bootmode.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/reboot.h>

#define SECONDS 5

int main(void)
{
	printk("roundtrip app running from slot0 (built " __DATE__ " " __TIME__ ")\n");
	for (int i = SECONDS; i > 0; i--) {
		printk("back to bootloader in %d\n", i);
		k_sleep(K_SECONDS(1));
	}
	printk("bootmode_set(BOOTLOADER) -> %d, rebooting\n", bootmode_set(BOOT_MODE_TYPE_BOOTLOADER));
	k_sleep(K_MSEC(10));
	sys_reboot(SYS_REBOOT_WARM);
	return 0;
}
