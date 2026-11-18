/*
 * SPDX-License-Identifier: MIT
 *
 * Minimal driver for the JDI LPM013M126A 176x176 8-color memory LCD.
 */

#ifndef LPM_PANEL_H
#define LPM_PANEL_H

#include <stdbool.h>
#include <stdint.h>

#define LPM_W 176
#define LPM_H 176
#define LPM_ROW_BYTES (LPM_W * 3 / 8) /* 66: 3 bits per pixel, R G B */

/* 3-bit colors, bit2 = R, bit1 = G, bit0 = B. 1 = lit (white = all on). */
enum lpm_color {
	LPM_BLACK = 0,
	LPM_BLUE = 1,
	LPM_GREEN = 2,
	LPM_CYAN = 3,
	LPM_RED = 4,
	LPM_MAGENTA = 5,
	LPM_YELLOW = 6,
	LPM_WHITE = 7,
};

/* Power-on sequence: all-clear, DISP on, start EXTCOMIN. */
int lpm_panel_init(void);

/* Back buffer to draw into: LPM_H rows of LPM_ROW_BYTES. */
uint8_t *lpm_panel_buffer(void);

/* Send every row that differs from what the panel shows. Returns rows sent. */
int lpm_panel_flush(void);

/* Power-off sequence: all-clear, stop EXTCOMIN, DISP off. */
void lpm_panel_off(void);

bool lpm_panel_ready(void);

#endif
