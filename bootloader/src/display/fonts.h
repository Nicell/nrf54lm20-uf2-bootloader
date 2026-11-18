/*
 * SPDX-License-Identifier: MIT
 */

#ifndef UF2_FONTS_H
#define UF2_FONTS_H

#include <stdint.h>

/*
 * Bitmap font, row-major, MSB = leftmost pixel. Drawn proportionally:
 * each glyph advances by its ink width plus `track` pixels.
 */
struct font {
	uint8_t w, h, stride;
	uint8_t first, last;
	uint8_t track;
	const uint8_t *bits;            /* glyph i at bits[i * stride * h] */
	const uint8_t (*metrics)[2];    /* {first ink column, ink width} */
};

extern const struct font font_1016; /* 10x16 */
extern const struct font font_1524; /* 15x24 */
extern const struct font font_2032; /* 20x32 */

#endif
