/*
 * SPDX-License-Identifier: MIT
 *
 * JDI LPM013M126A driver (spec Ver.01, 2016-07-20).
 *
 * Why not Zephyr's jdi,lpm013m126 driver: it bit-reverses the gate
 * address (the panel takes AG9..AG0 MSB-first, plain line numbers,
 * spec section 8), omits the 16 trailing clocks (6.1/6.2), sends one
 * transaction per line, and needs a 60 KB RGB565 frame.
 *
 * Multi-line update (6.2), MSB first, SCS high for the whole burst:
 *   [M0..M5 AG9 AG8] [AG7..AG0] [66 data bytes]   first line, M0 = 1
 *   [6 dummy AG9 AG8] [AG7..AG0] [66 data bytes]   each further line
 *   [16 dummy clocks]
 * so every line is the same 68-byte record except the first header
 * byte. Lines are numbered 1..176.
 */

#include <string.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "lpm_panel.h"

LOG_MODULE_REGISTER(lpm_panel, LOG_LEVEL_INF);

#define NODE DT_CHOSEN(zephyr_display)
BUILD_ASSERT(DT_NODE_HAS_COMPAT(NODE, jdi_lpm013m126), "zephyr,display must be an LPM013M126");
BUILD_ASSERT(DT_PROP(NODE, width) == LPM_W && DT_PROP(NODE, height) == LPM_H);

#define M0_UPDATE 0x80
#define M2_ALLCLEAR 0x20
#define REC_BYTES (2 + LPM_ROW_BYTES)

static const struct spi_dt_spec spi =
	SPI_DT_SPEC_GET(NODE, SPI_OP_MODE_MASTER | SPI_WORD_SET(8) | SPI_TRANSFER_MSB);
static const struct gpio_dt_spec disp = GPIO_DT_SPEC_GET(NODE, disp_gpios);
static const struct gpio_dt_spec extcomin = GPIO_DT_SPEC_GET(NODE, extcomin_gpios);

static uint8_t back[LPM_H][LPM_ROW_BYTES];
static uint8_t front[LPM_H][LPM_ROW_BYTES];
static uint8_t txbuf[LPM_H * REC_BYTES + 2];
static bool ready;

static void extcomin_toggle(struct k_timer *t)
{
	ARG_UNUSED(t);
	gpio_pin_toggle_dt(&extcomin);
}
static K_TIMER_DEFINE(extcomin_timer, extcomin_toggle, NULL);

static int send(const uint8_t *buf, size_t len)
{
	const struct spi_buf b = {.buf = (void *)buf, .len = len};
	const struct spi_buf_set set = {.buffers = &b, .count = 1};

	return spi_write_dt(&spi, &set);
}

static int all_clear(void)
{
	const uint8_t cmd[2] = {M2_ALLCLEAR, 0x00};

	return send(cmd, sizeof(cmd));
}

int lpm_panel_init(void)
{
	int rc;

	if (!spi_is_ready_dt(&spi) || !gpio_is_ready_dt(&disp) || !gpio_is_ready_dt(&extcomin)) {
		LOG_ERR("display bus or pins not ready");
		return -ENODEV;
	}
	rc = gpio_pin_configure_dt(&disp, GPIO_OUTPUT_INACTIVE);
	rc = rc ?: gpio_pin_configure_dt(&extcomin, GPIO_OUTPUT_INACTIVE);
	if (rc) {
		return rc;
	}

	/* T2: pixel memory init (>= 1 ms), T3/T4: latch + COM init (>= 30 us). */
	rc = all_clear();
	if (rc) {
		LOG_ERR("all clear: %d", rc);
		return rc;
	}
	k_busy_wait(1000);
	memset(front, 0, sizeof(front)); /* all clear = black */

	gpio_pin_set_dt(&disp, 1);
	k_busy_wait(100);
	k_timer_start(&extcomin_timer, K_NO_WAIT,
		      K_MSEC(1000 / DT_PROP(NODE, extcomin_frequency) / 2));
	ready = true;
	return 0;
}

bool lpm_panel_ready(void)
{
	return ready;
}

uint8_t *lpm_panel_buffer(void)
{
	return &back[0][0];
}

int lpm_panel_flush(void)
{
	uint8_t *p = txbuf;
	int rows = 0;

	if (!ready) {
		return -ENODEV;
	}

	for (int y = 0; y < LPM_H; y++) {
		if (memcmp(back[y], front[y], LPM_ROW_BYTES) == 0) {
			continue;
		}
		uint16_t line = (uint16_t)(y + 1);

		*p++ = (rows == 0 ? M0_UPDATE : 0x00) | ((line >> 8) & 0x03);
		*p++ = line & 0xFF;
		memcpy(p, back[y], LPM_ROW_BYTES);
		p += LPM_ROW_BYTES;
		memcpy(front[y], back[y], LPM_ROW_BYTES);
		rows++;
	}
	if (rows == 0) {
		return 0;
	}
	*p++ = 0x00; /* 16 trailing clocks */
	*p++ = 0x00;

	int rc = send(txbuf, p - txbuf);

	if (rc) {
		LOG_ERR("flush: %d", rc);
		return rc;
	}
	return rows;
}

void lpm_panel_off(void)
{
	if (!ready) {
		return;
	}
	/* T5: pixel memory init, T6: >= 30 us, then stop COM and DISP. */
	(void)all_clear();
	k_busy_wait(100);
	k_timer_stop(&extcomin_timer);
	gpio_pin_set_dt(&extcomin, 0);
	gpio_pin_set_dt(&disp, 0);
	ready = false;
}
