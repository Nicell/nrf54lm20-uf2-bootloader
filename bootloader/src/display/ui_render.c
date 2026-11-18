/*
 * SPDX-License-Identifier: MIT
 *
 * 176x176, 8 colors. Designed for a black case: black background so the
 * panel blends into the enclosure, white for content, color only as an
 * accent (cyan = activity, green = success, red = failure). One icon and
 * at most a couple of words per screen.
 *
 *   6..21     side label (small, blue = dim on black)
 *   30..120   hero: icon, or big percentage + progress bar
 *   130..153  one short word or phrase
 */

#include <stdio.h>

#include "gfx.h"
#include "ui_render.h"

#define W LPM_W
#define CX (W / 2)
#define HERO_Y 76
#define WORD_Y 132

#define FG LPM_WHITE
#define BG LPM_BLACK
#define ACCENT LPM_CYAN

/* The USB trident. */
static void icon_usb(int cx, int cy, enum lpm_color c)
{
	gfx_line(cx, cy + 26, cx, cy - 22, 5, c);
	gfx_line(cx - 8, cy - 18, cx, cy - 30, 5, c);
	gfx_line(cx + 8, cy - 18, cx, cy - 30, 5, c);
	gfx_disc(cx, cy + 28, 7, c);
	gfx_line(cx, cy + 12, cx - 20, cy - 2, 5, c);
	gfx_line(cx - 20, cy - 2, cx - 20, cy - 10, 5, c);
	gfx_disc(cx - 20, cy - 13, 6, c);
	gfx_line(cx, cy + 6, cx + 20, cy - 8, 5, c);
	gfx_line(cx + 20, cy - 8, cx + 20, cy - 14, 5, c);
	gfx_fill(cx + 15, cy - 25, 11, 11, c);
}

/* Arrow dropping into a tray. */
static void icon_drop(int cx, int cy)
{
	gfx_fill(cx - 3, cy - 32, 7, 34, ACCENT);
	gfx_line(cx - 15, cy - 12, cx, cy + 3, 7, ACCENT);
	gfx_line(cx + 15, cy - 12, cx, cy + 3, 7, ACCENT);
	gfx_round_fill(cx - 30, cy + 2, 6, 28, 3, FG);
	gfx_round_fill(cx + 24, cy + 2, 6, 28, 3, FG);
	gfx_round_fill(cx - 30, cy + 24, 60, 6, 3, FG);
}

/* Ring with a mark inside, in an accent color. */
static void ring(int cx, int cy, enum lpm_color c)
{
	gfx_disc(cx, cy, 32, c);
	gfx_disc(cx, cy, 27, BG);
}

static void icon_check(int cx, int cy)
{
	ring(cx, cy, LPM_GREEN);
	gfx_line(cx - 13, cy + 1, cx - 4, cy + 10, 6, LPM_GREEN);
	gfx_line(cx - 4, cy + 10, cx + 14, cy - 9, 6, LPM_GREEN);
}

static void icon_cross(int cx, int cy)
{
	ring(cx, cy, LPM_RED);
	gfx_line(cx - 10, cy - 10, cx + 10, cy + 10, 6, LPM_RED);
	gfx_line(cx + 10, cy - 10, cx - 10, cy + 10, 6, LPM_RED);
}

static void progress(uint32_t done, uint32_t total, int y)
{
	const int x = 20, w = W - 40, h = 8;
	int fill = total ? (int)((uint64_t)w * done / total) : 0;

	gfx_round_fill(x, y, w, h, 4, LPM_BLUE);        /* track: dim */
	if (fill > 0) {
		gfx_round_fill(x, y, fill < h ? h : fill, h, 4, ACCENT);
	}
}

static const char *error_word(enum uf2_error e)
{
	switch (e) {
	case UF2_ERR_RANGE:
	case UF2_ERR_TOO_BIG:
	case UF2_ERR_INVALID_APP: return "Wrong file";
	case UF2_ERR_BAD_BLOCK:
	case UF2_ERR_ALIGN:
	case UF2_ERR_NO_VECTORS:  return "Bad file";
	default:                  return "Write failed";
	}
}

void ui_render(const struct ui_model *m)
{
	const struct uf2_status *st = &m->st;
	char buf[8];

	gfx_clear(BG);
	if (m->badge && *m->badge) {
		gfx_text_center(&font_1016, CX, 4, m->badge, LPM_BLUE);
	}

	switch (st->state) {
	case UF2_STATE_CONNECTED:
		icon_drop(CX, HERO_Y);
		gfx_text_center(&font_1524, CX, WORD_Y, "Drop .uf2", FG);
		break;

	case UF2_STATE_WRITING: {
		uint32_t pct = st->blocks_total ?
			(uint32_t)((uint64_t)st->blocks_written * 100U / st->blocks_total) : 0;

		snprintf(buf, sizeof(buf), "%u%%", (unsigned)pct);
		gfx_text_center(&font_2032, CX, HERO_Y - 22, buf, FG);
		progress(st->blocks_written, st->blocks_total, HERO_Y + 20);
		gfx_text_center(&font_1524, CX, WORD_Y, "Updating", FG);
		break;
	}

	case UF2_STATE_DONE:
		icon_check(CX, HERO_Y);
		gfx_text_center(&font_1524, CX, WORD_Y, "Done", FG);
		break;

	case UF2_STATE_ERROR:
		icon_cross(CX, HERO_Y);
		gfx_text_center(&font_1524, CX, WORD_Y, error_word(st->error), FG);
		break;

	default: /* waiting for a USB host */
		icon_usb(CX, HERO_Y, FG);
		gfx_text_center(&font_1524, CX, WORD_Y, "Connect", FG);
		break;
	}
}
