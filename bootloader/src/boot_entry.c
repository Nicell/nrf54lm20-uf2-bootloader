/*
 * SPDX-License-Identifier: MIT
 *
 * DFU entry decision, in priority order:
 *
 *   1. Boot-mode retention says BOOTLOADER. This is Zephyr's standard
 *      boot-mode API (gpregret byte), which ZMK's &bootloader behavior
 *      writes before sys_reboot(). Always cleared so it fires once.
 *   2. The dfu-key matrix key (compatible "uf2,matrix-key") is held.
 *   3. The dfu-button alias is held.
 *   4. slot0 has no valid vector table.
 *   5. Optional double-tap: arm the same boot-mode flag for a window; a
 *      reset inside the window lands in case 1 on the next boot.
 */

#include <zephyr/kernel.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>
#include <zephyr/retention/bootmode.h>

#include "app_image.h"
#include "boot_entry.h"

LOG_MODULE_REGISTER(boot_entry, LOG_LEVEL_INF);

#define DFU_KEY_NODE DT_INST(0, uf2_matrix_key)
#define DFU_BUTTON_NODE DT_ALIAS(dfu_button)

static bool boot_mode_requested(void)
{
	int rc = bootmode_check(BOOT_MODE_TYPE_BOOTLOADER);

	(void)bootmode_clear();
	return rc > 0;
}

#if DT_NODE_HAS_STATUS_OKAY(DFU_KEY_NODE)
static const struct gpio_dt_spec key_drive = GPIO_DT_SPEC_GET(DFU_KEY_NODE, drive_gpios);
static const struct gpio_dt_spec key_sense = GPIO_DT_SPEC_GET(DFU_KEY_NODE, sense_gpios);

/* One scan: the key counts only if sense follows drive (idle low, driven high). */
static bool dfu_key_sample(void)
{
	int idle, driven;

	(void)gpio_pin_set_dt(&key_drive, 0);
	k_busy_wait(20);
	idle = gpio_pin_get_dt(&key_sense);

	(void)gpio_pin_set_dt(&key_drive, 1);
	k_busy_wait(20);
	driven = gpio_pin_get_dt(&key_sense);

	(void)gpio_pin_set_dt(&key_drive, 0);
	return idle == 0 && driven == 1;
}

static bool dfu_key_held(void)
{
	bool held;

	if (!gpio_is_ready_dt(&key_drive) || !gpio_is_ready_dt(&key_sense) ||
	    gpio_pin_configure_dt(&key_sense, GPIO_INPUT) ||
	    gpio_pin_configure_dt(&key_drive, GPIO_OUTPUT_INACTIVE)) {
		LOG_WRN("dfu-key GPIO setup failed");
		return false;
	}

	/* Two scans 5 ms apart reject contact bounce while plugging in. */
	held = dfu_key_sample();
	if (held) {
		k_busy_wait(5000);
		held = dfu_key_sample();
	}

	(void)gpio_pin_configure_dt(&key_drive, GPIO_DISCONNECTED);
	(void)gpio_pin_configure_dt(&key_sense, GPIO_DISCONNECTED);
	return held;
}
#else
static bool dfu_key_held(void)
{
	return false;
}
#endif

#if DT_NODE_EXISTS(DFU_BUTTON_NODE)
static const struct gpio_dt_spec dfu_button = GPIO_DT_SPEC_GET(DFU_BUTTON_NODE, gpios);

static bool dfu_button_held(void)
{
	bool held;

	if (!gpio_is_ready_dt(&dfu_button) || gpio_pin_configure_dt(&dfu_button, GPIO_INPUT)) {
		return false;
	}
	held = gpio_pin_get_dt(&dfu_button) > 0;
	(void)gpio_pin_configure_dt(&dfu_button, GPIO_DISCONNECTED);
	return held;
}
#else
static bool dfu_button_held(void)
{
	return false;
}
#endif

enum uf2_entry_reason boot_entry_check(void)
{
	if (boot_mode_requested()) {
		/* Also how a double tap arrives; both look the same here. */
		return IS_ENABLED(CONFIG_UF2_DOUBLE_TAP) ? UF2_ENTRY_DOUBLE_TAP
							 : UF2_ENTRY_BOOT_MODE;
	}
	if (dfu_key_held()) {
		return UF2_ENTRY_KEY;
	}
	if (dfu_button_held()) {
		return UF2_ENTRY_BUTTON;
	}
	if (!app_image_valid()) {
		return UF2_ENTRY_NO_APP;
	}

#if defined(CONFIG_UF2_DOUBLE_TAP)
	(void)bootmode_set(BOOT_MODE_TYPE_BOOTLOADER);
	k_busy_wait(CONFIG_UF2_DOUBLE_TAP_DELAY_MS * 1000U);
	(void)bootmode_clear();
#endif
	return UF2_ENTRY_NONE;
}
