/*
 * SPDX-License-Identifier: MIT
 */

#include <string.h>

#include "gfx.h"

static inline uint8_t *row(int y)
{
	return lpm_panel_buffer() + y * LPM_ROW_BYTES;
}

void gfx_px(int x, int y, enum lpm_color c)
{
	if ((unsigned)x >= LPM_W || (unsigned)y >= LPM_H) {
		return;
	}
	uint8_t *r = row(y);
	int bit = x * 3;

	for (int i = 0; i < 3; i++, bit++) {
		uint8_t m = 0x80 >> (bit & 7);

		if (c & (4 >> i)) {
			r[bit >> 3] |= m;
		} else {
			r[bit >> 3] &= ~m;
		}
	}
}

void gfx_fill(int x, int y, int w, int h, enum lpm_color c)
{
	int x1 = x + w, y1 = y + h;

	x = x < 0 ? 0 : x;
	y = y < 0 ? 0 : y;
	x1 = x1 > LPM_W ? LPM_W : x1;
	y1 = y1 > LPM_H ? LPM_H : y1;
	if (x >= x1 || y >= y1) {
		return;
	}
	if (x == 0 && x1 == LPM_W) {
		/* Full rows: 8 pixels = 3 bytes repeat. */
		uint8_t pat[3];
		uint32_t v = 0;

		for (int i = 0; i < 8; i++) {
			v = (v << 3) | c;
		}
		pat[0] = v >> 16;
		pat[1] = v >> 8;
		pat[2] = v;
		for (int yy = y; yy < y1; yy++) {
			for (int b = 0; b < LPM_ROW_BYTES; b++) {
				row(yy)[b] = pat[b % 3];
			}
		}
		return;
	}
	for (int yy = y; yy < y1; yy++) {
		for (int xx = x; xx < x1; xx++) {
			gfx_px(xx, yy, c);
		}
	}
}

void gfx_clear(enum lpm_color c)
{
	gfx_fill(0, 0, LPM_W, LPM_H, c);
}

void gfx_frame(int x, int y, int w, int h, int t, enum lpm_color c)
{
	gfx_fill(x, y, w, t, c);
	gfx_fill(x, y + h - t, w, t, c);
	gfx_fill(x, y, t, h, c);
	gfx_fill(x + w - t, y, t, h, c);
}

void gfx_disc(int cx, int cy, int r, enum lpm_color c)
{
	for (int dy = -r; dy <= r; dy++) {
		for (int dx = -r; dx <= r; dx++) {
			if (dx * dx + dy * dy <= r * r + r) {
				gfx_px(cx + dx, cy + dy, c);
			}
		}
	}
}

void gfx_round_fill(int x, int y, int w, int h, int r, enum lpm_color c)
{
	if (r * 2 > h) {
		r = h / 2;
	}
	if (r * 2 > w) {
		r = w / 2;
	}
	gfx_fill(x + r, y, w - 2 * r, h, c);
	gfx_fill(x, y + r, w, h - 2 * r, c);
	for (int dy = 0; dy < r; dy++) {
		for (int dx = 0; dx < r; dx++) {
			int ex = r - dx - 1, ey = r - dy - 1; /* distance to corner centre */

			if (ex * ex + ey * ey <= r * r) {
				gfx_px(x + dx, y + dy, c);
				gfx_px(x + w - 1 - dx, y + dy, c);
				gfx_px(x + dx, y + h - 1 - dy, c);
				gfx_px(x + w - 1 - dx, y + h - 1 - dy, c);
			}
		}
	}
}

void gfx_line(int x0, int y0, int x1, int y1, int thick, enum lpm_color c)
{
	int dx = x1 > x0 ? x1 - x0 : x0 - x1, sx = x0 < x1 ? 1 : -1;
	int dy = y1 > y0 ? y0 - y1 : y1 - y0, sy = y0 < y1 ? 1 : -1;
	int err = dx + dy;
	int r = thick / 2;

	for (;;) {
		if (r) {
			gfx_disc(x0, y0, r, c);
		} else {
			gfx_px(x0, y0, c);
		}
		if (x0 == x1 && y0 == y1) {
			break;
		}
		int e2 = 2 * err;

		if (e2 >= dy) {
			err += dy;
			x0 += sx;
		}
		if (e2 <= dx) {
			err += dx;
			y0 += sy;
		}
	}
}

static const uint8_t *glyph(const struct font *f, unsigned ch, const uint8_t **m)
{
	if (ch < f->first || ch > f->last) {
		ch = (f->first <= '?' && '?' <= f->last) ? '?' : f->first;
	}
	*m = f->metrics[ch - f->first];
	return f->bits + (ch - f->first) * f->stride * f->h;
}

int gfx_text(const struct font *f, int x, int y, const char *s, enum lpm_color c)
{
	for (; *s; s++) {
		const uint8_t *m;
		const uint8_t *g = glyph(f, (unsigned char)*s, &m);

		for (int yy = 0; yy < f->h; yy++, g += f->stride) {
			for (int xx = m[0]; xx < m[0] + m[1]; xx++) {
				if (g[xx >> 3] & (0x80 >> (xx & 7))) {
					gfx_px(x + xx - m[0], y + yy, c);
				}
			}
		}
		x += m[1] + f->track;
	}
	return x;
}

int gfx_text_width(const struct font *f, const char *s)
{
	int w = 0;

	for (; *s; s++) {
		const uint8_t *m;

		(void)glyph(f, (unsigned char)*s, &m);
		w += m[1] + f->track;
	}
	return w ? w - f->track : 0;
}

void gfx_text_center(const struct font *f, int cx, int y, const char *s, enum lpm_color c)
{
	gfx_text(f, cx - gfx_text_width(f, s) / 2, y, s, c);
}
