/*
 * SPDX-License-Identifier: MIT
 *
 * USB device context setup.
 *
 * Modelled after ZMK's app/src/usb_hid.c. The key nRF54L-specific
 * pattern: the USBHS controller's UDC driver advertises that it can
 * detect VBUS itself (via the NRFS secure-domain service). When that
 * is the case, do NOT call usbd_enable() at init time; instead defer
 * it to the USBD_MSG_VBUS_READY callback. Enabling synchronously
 * while VBUS handshake is still in flight leaves DWC2 in a state
 * where enumeration stalls after the host's USB reset.
 */

#include <zephyr/device.h>
#include <zephyr/logging/log.h>
#include <zephyr/usb/usbd.h>

#include "uf2_status.h"
#include "usbd_setup.h"

LOG_MODULE_REGISTER(uf2_usbd, LOG_LEVEL_INF);

USBD_DEVICE_DEFINE(uf2_usbd,
                   DEVICE_DT_GET(DT_NODELABEL(zephyr_udc0)),
                   CONFIG_UF2_USBD_VID, CONFIG_UF2_USBD_PID);

USBD_DESC_LANG_DEFINE(uf2_lang);
USBD_DESC_MANUFACTURER_DEFINE(uf2_mfr, CONFIG_UF2_USBD_MANUFACTURER);
USBD_DESC_PRODUCT_DEFINE(uf2_product, CONFIG_UF2_USBD_PRODUCT);
IF_ENABLED(CONFIG_HWINFO, (USBD_DESC_SERIAL_NUMBER_DEFINE(uf2_sn)));

USBD_DESC_CONFIG_DEFINE(fs_cfg_desc, "FS Configuration");
USBD_DESC_CONFIG_DEFINE(hs_cfg_desc, "HS Configuration");

#define UF2_USBD_ATTRS  USB_SCD_SELF_POWERED
#define UF2_USBD_MAX_MA 250

USBD_CONFIGURATION_DEFINE(uf2_fs_config, UF2_USBD_ATTRS, UF2_USBD_MAX_MA, &fs_cfg_desc);
USBD_CONFIGURATION_DEFINE(uf2_hs_config, UF2_USBD_ATTRS, UF2_USBD_MAX_MA, &hs_cfg_desc);

static void uf2_usbd_msg_cb(struct usbd_context *ctx, const struct usbd_msg *msg)
{
    LOG_DBG("USBD: %s", usbd_msg_type_string(msg->type));

    switch (msg->type) {
    case USBD_MSG_VBUS_READY:
        if (usbd_can_detect_vbus(ctx)) {
            int err = usbd_enable(ctx);
            if (err) {
                LOG_ERR("usbd_enable on VBUS_READY: %d", err);
            }
        }
        break;
    case USBD_MSG_VBUS_REMOVED:
        if (usbd_can_detect_vbus(ctx)) {
            (void)usbd_disable(ctx);
        }
        uf2_status_set_state(UF2_STATE_WAITING);
        break;
    case USBD_MSG_CONFIGURATION:
        uf2_status_set_state(UF2_STATE_CONNECTED);
        break;
    case USBD_MSG_SUSPEND:
        uf2_status_set_state(UF2_STATE_WAITING);
        break;
    case USBD_MSG_RESUME:
        uf2_status_set_state(UF2_STATE_CONNECTED);
        break;
    default:
        break;
    }
}

struct usbd_context *uf2_usbd_init_device(void)
{
    int err;

    err = usbd_add_descriptor(&uf2_usbd, &uf2_lang);
    err = err ?: usbd_add_descriptor(&uf2_usbd, &uf2_mfr);
    err = err ?: usbd_add_descriptor(&uf2_usbd, &uf2_product);
    IF_ENABLED(CONFIG_HWINFO, (
        err = err ?: usbd_add_descriptor(&uf2_usbd, &uf2_sn);
    ))
    if (err) {
        LOG_ERR("descriptor add: %d", err);
        return NULL;
    }

    if (USBD_SUPPORTS_HIGH_SPEED &&
        usbd_caps_speed(&uf2_usbd) == USBD_SPEED_HS) {
        err = usbd_add_configuration(&uf2_usbd, USBD_SPEED_HS, &uf2_hs_config);
        err = err ?: usbd_register_all_classes(&uf2_usbd, USBD_SPEED_HS, 1, NULL);
        if (err) {
            LOG_ERR("HS config: %d", err);
            return NULL;
        }
        usbd_device_set_code_triple(&uf2_usbd, USBD_SPEED_HS, 0, 0, 0);
    }

    err = usbd_add_configuration(&uf2_usbd, USBD_SPEED_FS, &uf2_fs_config);
    err = err ?: usbd_register_all_classes(&uf2_usbd, USBD_SPEED_FS, 1, NULL);
    if (err) {
        LOG_ERR("FS config: %d", err);
        return NULL;
    }
    usbd_device_set_code_triple(&uf2_usbd, USBD_SPEED_FS, 0, 0, 0);
    usbd_self_powered(&uf2_usbd, true);

    err = usbd_msg_register_cb(&uf2_usbd, uf2_usbd_msg_cb);
    if (err) {
        LOG_ERR("msg_register_cb: %d", err);
        return NULL;
    }

    err = usbd_init(&uf2_usbd);
    if (err) {
        LOG_ERR("usbd_init: %d", err);
        return NULL;
    }

    /* nRF54L: defer enable until USBD_MSG_VBUS_READY arrives on the
     * callback. For controllers that can't detect VBUS, bring it up
     * synchronously now.
     */
    if (!usbd_can_detect_vbus(&uf2_usbd)) {
        err = usbd_enable(&uf2_usbd);
        if (err) {
            LOG_ERR("usbd_enable: %d", err);
            return NULL;
        }
    }

    return &uf2_usbd;
}
