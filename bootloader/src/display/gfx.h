/*
 * SPDX-License-Identifier: MIT
 *
 * Tiny 3-bit drawing layer over the LPM013M126A back buffer.
 */

#ifndef UF2_GFX_H
#define UF2_GFX_H

#include <stdint.h>

#include "fonts.h"
#include "lpm_panel.h"

void gfx_clear(enum lpm_color c);
void gfx_px(int x, int y, enum lpm_color c);
void gfx_fill(int x, int y, int w, int h, enum lpm_color c);
void gfx_frame(int x, int y, int w, int h, int t, enum lpm_color c);
void gfx_round_fill(int x, int y, int w, int h, int r, enum lpm_color c);
void gfx_disc(int cx, int cy, int r, enum lpm_color c);
void gfx_line(int x0, int y0, int x1, int y1, int thick, enum lpm_color c);

/* Text: transparent background; returns the x after the last glyph. */
int gfx_text(const struct font *f, int x, int y, const char *s, enum lpm_color c);
int gfx_text_width(const struct font *f, const char *s);
void gfx_text_center(const struct font *f, int cx, int y, const char *s, enum lpm_color c);

#endif
