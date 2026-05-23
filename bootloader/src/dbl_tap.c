/*
 * SPDX-License-Identifier: MIT
 *
 * Double-tap reset boot trigger.
 *
 * One 32-bit word in noinit RAM persists across warm resets. The protocol:
 *
 *   - If the word is DBL_TAP_MAGIC at startup, the user just hit reset
 *     twice in quick succession (or an application set the flag and
 *     issued sys_reboot()): clear the flag and enter DFU mode.
 *
 *   - If the word is DBL_TAP_MAGIC_QUICK_BOOT, an application requested
 *     a fast skip-the-window boot path: clear and jump straight to the
 *     application.
 *
 *   - Otherwise this is a normal cold boot or a single tap of reset.
 *     Write the magic, hold an LED hint and busy-wait for the configured
 *     window. If a second reset arrives during the window, the magic
 *     stays set and the next boot enters DFU. If it doesn't, clear the
 *     magic and fall through to the application.
 *
 * Additionally: if slot0 looks empty (reset vector reads as 0xFFFFFFFF),
 * we always enter DFU regardless of the magic, so a freshly-erased
 * device is always usable.
 */

#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>
#include <zephyr/logging/log.h>
#include <zephyr/drivers/gpio.h>

#include "board_flash.h"
#include "dbl_tap.h"

/*
 * Optional GPIO trigger: if an `aliases { dfu-button = ...; }` exists
 * in DT and the pin reads asserted at boot, force DFU regardless of
 * the noinit magic or slot0 state. Useful on dev boards where the
 * reset button doesn't preserve SRAM across resets.
 */
#define DFU_BUTTON_NODE DT_ALIAS(dfu_button)
#if DT_NODE_EXISTS(DFU_BUTTON_NODE)
#define HAS_DFU_BUTTON 1
static const struct gpio_dt_spec dfu_button =
    GPIO_DT_SPEC_GET(DFU_BUTTON_NODE, gpios);
#else
#define HAS_DFU_BUTTON 0
#endif

LOG_MODULE_REGISTER(dbl_tap, LOG_LEVEL_INF);

/*
 * Placed in .noinit so the loader leaves it untouched across warm
 * resets. The address must NOT live in a region that gets zeroed by
 * the SoC reset (cpuapp_sram is zeroed only by the .bss init code, not
 * by the SoC itself).
 */
static uint32_t __noinit dbl_tap_mark;

static bool app_present(void)
{
    /* The reset vector of a Zephyr image lives at slot0+0 (initial MSP)
     * and slot0+4 (reset handler PC). A freshly-erased RRAM region
     * reads as 0xFFFFFFFF. If either of the first two words is all-ones
     * we treat the slot as empty.
     */
    uint32_t sp, pc;
    board_flash_read(BOARD_FLASH_APP_START + 0, &sp, sizeof(sp));
    board_flash_read(BOARD_FLASH_APP_START + 4, &pc, sizeof(pc));
    return (sp != 0xFFFFFFFFU) && (pc != 0xFFFFFFFFU);
}

static bool dfu_button_pressed(void)
{
#if HAS_DFU_BUTTON
    if (!gpio_is_ready_dt(&dfu_button)) {
        return false;
    }
    int rc = gpio_pin_configure_dt(&dfu_button, GPIO_INPUT);
    if (rc) {
        return false;
    }
    /* gpio_pin_get_dt honours the GPIO_ACTIVE_LOW flag from DT, so a
     * non-zero return means the button is asserted regardless of
     * electrical polarity.
     */
    return gpio_pin_get_dt(&dfu_button) > 0;
#else
    return false;
#endif
}

bool dbl_tap_should_enter_dfu(void)
{
    uint32_t mark = dbl_tap_mark;
    dbl_tap_mark = 0;

    if (dfu_button_pressed()) {
        LOG_INF("DFU button held -> DFU");
        return true;
    }

    if (!app_present()) {
        LOG_INF("slot0 is empty -> DFU");
        return true;
    }

    switch (mark) {
    case DBL_TAP_MAGIC:
        LOG_INF("double-tap detected -> DFU");
        return true;
    case DBL_TAP_MAGIC_QUICK_BOOT:
        LOG_INF("quick boot requested -> app");
        return false;
    default:
        break;
    }

    /* Arm the double-tap window. If the user releases and taps reset
     * again during the wait, the next boot will enter DFU above.
     */
    dbl_tap_mark = DBL_TAP_MAGIC;
    LOG_INF("arming double-tap window (%d ms)", CONFIG_UF2_DBL_TAP_DELAY_MS);
    k_busy_wait(CONFIG_UF2_DBL_TAP_DELAY_MS * 1000U);
    dbl_tap_mark = 0;

    return false;
}
