/*
 * SPDX-License-Identifier: MIT
 *
 * Pure drawing of the bootloader screen from a status snapshot. No Zephyr
 * dependencies, so tests/host/ui_preview can render it to PNG.
 */

#ifndef UF2_UI_RENDER_H
#define UF2_UI_RENDER_H

#include <stdint.h>

#include "../uf2_status.h"

struct ui_model {
	struct uf2_status st;
	const char *badge;   /* small label at the top, e.g. "LEFT"; may be "" */
	uint32_t rate_kbps;  /* KB/s while writing, 0 if unknown (not drawn yet) */
};

void ui_render(const struct ui_model *m);

#endif
