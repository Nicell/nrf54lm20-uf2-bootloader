/*
 * SPDX-License-Identifier: MIT
 *
 * Bootloader status bus. The core publishes state, entry reason and
 * progress; any number of listeners (PWM LED, display, ...) observe it.
 *
 * Define a listener anywhere with UF2_STATUS_LISTENER_DEFINE(). Callbacks
 * run synchronously in the publisher's context, which may be the USB MSC
 * thread in the middle of a transfer: copy what you need and return. Slow
 * work (SPI display refresh) belongs on your own work item or thread.
 */

#ifndef UF2_STATUS_H
#define UF2_STATUS_H

#include <stdint.h>
#include <zephyr/sys/iterable_sections.h>

enum uf2_state {
	UF2_STATE_BOOTING = 0, /* deciding whether to stay in DFU          */
	UF2_STATE_WAITING,     /* in DFU, no USB host configured us yet     */
	UF2_STATE_CONNECTED,   /* host enumerated the drive                 */
	UF2_STATE_WRITING,     /* UF2 transfer in progress                  */
	UF2_STATE_DONE,        /* image committed, about to reboot into it  */
	UF2_STATE_ERROR,       /* transfer rejected; see uf2_status.error   */
};

enum uf2_entry_reason {
	UF2_ENTRY_NONE = 0,
	UF2_ENTRY_NO_APP,      /* slot0 has no valid image                  */
	UF2_ENTRY_BOOT_MODE,   /* app requested it (ZMK &bootloader)        */
	UF2_ENTRY_KEY,         /* dfu-key held at boot                      */
	UF2_ENTRY_BUTTON,      /* dfu-button alias held at boot             */
	UF2_ENTRY_DOUBLE_TAP,  /* reset pressed twice                       */
};

enum uf2_error {
	UF2_ERR_NONE = 0,
	UF2_ERR_BAD_BLOCK,     /* malformed UF2 header                      */
	UF2_ERR_RANGE,         /* target address outside the app slot       */
	UF2_ERR_ALIGN,         /* address/size not RRAM-write aligned       */
	UF2_ERR_TOO_BIG,       /* more blocks than the slot can hold        */
	UF2_ERR_WRITE,         /* flash driver returned an error            */
	UF2_ERR_VERIFY,        /* readback mismatch                         */
	UF2_ERR_NO_VECTORS,    /* image did not cover the app start         */
	UF2_ERR_INVALID_APP,   /* written image has an invalid vector table */
};

struct uf2_status {
	enum uf2_state state;
	enum uf2_entry_reason reason;
	enum uf2_error error;
	uint32_t blocks_written;
	uint32_t blocks_total;
	uint32_t bytes_written;
};

struct uf2_status_listener {
	void (*cb)(const struct uf2_status *status);
};

#define UF2_STATUS_LISTENER_DEFINE(_name, _cb)                                 \
	STRUCT_SECTION_ITERABLE(uf2_status_listener, _name) = { .cb = (_cb) }

const struct uf2_status *uf2_status_get(void);
void uf2_status_set_state(enum uf2_state state);
void uf2_status_set_reason(enum uf2_entry_reason reason);
void uf2_status_set_error(enum uf2_error error);
void uf2_status_set_progress(uint32_t written, uint32_t total, uint32_t bytes);

const char *uf2_state_str(enum uf2_state state);
const char *uf2_entry_reason_str(enum uf2_entry_reason reason);
const char *uf2_error_str(enum uf2_error error);

#endif /* UF2_STATUS_H */
