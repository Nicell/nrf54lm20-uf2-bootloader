/*
 * SPDX-License-Identifier: MIT
 *
 * Render every bootloader screen with the firmware's own drawing code
 * and write a contact sheet as a binary PPM (scaled 3x).
 * Run: make -C tests/host preview   ->  tests/host/ui_preview.ppm
 */

#include <stdio.h>
#include <string.h>

#include "display/lpm_panel.h"
#include "display/ui_render.h"

static uint8_t fb[LPM_H][LPM_ROW_BYTES];
uint8_t *lpm_panel_buffer(void) { return &fb[0][0]; }

#define SCALE 3
#define COLS 3
#define GAP 12

static const uint8_t rgb[8][3] = {
	{14, 14, 16}, {40, 60, 150}, {40, 160, 70}, {60, 170, 200},
	{210, 50, 45}, {190, 60, 170}, {230, 200, 40}, {228, 226, 218},
};

static int pixel(int x, int y)
{
	int bit = x * 3, v = 0;

	for (int i = 0; i < 3; i++, bit++) {
		v = (v << 1) | ((fb[y][bit >> 3] >> (7 - (bit & 7))) & 1);
	}
	return v;
}

int main(void)
{
	static const struct { enum uf2_state s; enum uf2_entry_reason r; enum uf2_error e;
			      uint32_t w, t, b, kbps; } shots[] = {
		{UF2_STATE_WAITING, UF2_ENTRY_KEY, 0, 0, 0, 0, 0},
		{UF2_STATE_CONNECTED, UF2_ENTRY_BOOT_MODE, 0, 0, 0, 0, 0},
		{UF2_STATE_WRITING, UF2_ENTRY_BOOT_MODE, 0, 1270, 2734, 325120, 830},
		{UF2_STATE_DONE, UF2_ENTRY_BOOT_MODE, 0, 2734, 2734, 699904, 0},
		{UF2_STATE_ERROR, UF2_ENTRY_BOOT_MODE, UF2_ERR_RANGE, 3, 2734, 768, 0},
		{UF2_STATE_CONNECTED, UF2_ENTRY_NO_APP, 0, 0, 0, 0, 0},
	};
	const int n = sizeof(shots) / sizeof(shots[0]);
	const int rows = (n + COLS - 1) / COLS;
	const int cw = LPM_W * SCALE, ch = LPM_H * SCALE;
	const int W = COLS * cw + (COLS + 1) * GAP, H = rows * ch + (rows + 1) * GAP;
	static uint8_t img[3 * 3 * 600 * 3 * 600];

	memset(img, 70, (size_t)W * H * 3);
	for (int i = 0; i < n; i++) {
		struct ui_model m = {
			.st = {.state = shots[i].s, .reason = shots[i].r, .error = shots[i].e,
			       .blocks_written = shots[i].w, .blocks_total = shots[i].t,
			       .bytes_written = shots[i].b},
			.badge = "LEFT",
			.rate_kbps = shots[i].kbps,
		};
		ui_render(&m);
		int ox = GAP + (i % COLS) * (cw + GAP), oy = GAP + (i / COLS) * (ch + GAP);

		for (int y = 0; y < ch; y++) {
			for (int x = 0; x < cw; x++) {
				const uint8_t *c = rgb[pixel(x / SCALE, y / SCALE)];
				uint8_t *d = &img[((size_t)(oy + y) * W + ox + x) * 3];

				d[0] = c[0]; d[1] = c[1]; d[2] = c[2];
			}
		}
	}
	FILE *f = fopen("ui_preview.ppm", "wb");

	fprintf(f, "P6\n%d %d\n255\n", W, H);
	fwrite(img, 1, (size_t)W * H * 3, f);
	fclose(f);
	printf("wrote ui_preview.ppm (%dx%d, %d screens)\n", W, H, n);
	return 0;
}
