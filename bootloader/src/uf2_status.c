/*
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "uf2_status.h"

LOG_MODULE_REGISTER(uf2_status, LOG_LEVEL_INF);

static struct uf2_status status;

static void publish(void)
{
	STRUCT_SECTION_FOREACH(uf2_status_listener, l) {
		l->cb(&status);
	}
}

const struct uf2_status *uf2_status_get(void)
{
	return &status;
}

void uf2_status_set_state(enum uf2_state state)
{
	if (status.state == state) {
		return;
	}
	/* An error sticks until a new transfer starts writing. */
	if (status.state == UF2_STATE_ERROR && state != UF2_STATE_WRITING) {
		return;
	}
	if (state == UF2_STATE_WRITING) {
		status.error = UF2_ERR_NONE;
	}
	status.state = state;
	LOG_INF("state: %s", uf2_state_str(state));
	publish();
}

void uf2_status_set_reason(enum uf2_entry_reason reason)
{
	status.reason = reason;
	publish();
}

void uf2_status_set_error(enum uf2_error error)
{
	status.error = error;
	status.state = UF2_STATE_ERROR;
	LOG_ERR("error: %s", uf2_error_str(error));
	publish();
}

void uf2_status_set_progress(uint32_t written, uint32_t total, uint32_t bytes)
{
	status.blocks_written = written;
	status.blocks_total = total;
	status.bytes_written = bytes;
	publish();
}

const char *uf2_state_str(enum uf2_state state)
{
	switch (state) {
	case UF2_STATE_BOOTING:   return "booting";
	case UF2_STATE_WAITING:   return "waiting for USB";
	case UF2_STATE_CONNECTED: return "connected";
	case UF2_STATE_WRITING:   return "writing";
	case UF2_STATE_DONE:      return "done";
	case UF2_STATE_ERROR:     return "error";
	}
	return "?";
}

const char *uf2_entry_reason_str(enum uf2_entry_reason reason)
{
	switch (reason) {
	case UF2_ENTRY_NONE:       return "none";
	case UF2_ENTRY_NO_APP:     return "no valid app";
	case UF2_ENTRY_BOOT_MODE:  return "requested by app";
	case UF2_ENTRY_KEY:        return "key held";
	case UF2_ENTRY_BUTTON:     return "button held";
	case UF2_ENTRY_DOUBLE_TAP: return "double tap";
	}
	return "?";
}

const char *uf2_error_str(enum uf2_error error)
{
	switch (error) {
	case UF2_ERR_NONE:        return "none";
	case UF2_ERR_BAD_BLOCK:   return "malformed UF2 block";
	case UF2_ERR_RANGE:       return "address outside app slot";
	case UF2_ERR_ALIGN:       return "unaligned write";
	case UF2_ERR_TOO_BIG:     return "image too large";
	case UF2_ERR_WRITE:       return "flash write failed";
	case UF2_ERR_VERIFY:      return "verify failed";
	case UF2_ERR_NO_VECTORS:  return "image missing app start";
	case UF2_ERR_INVALID_APP: return "invalid vector table";
	}
	return "?";
}
