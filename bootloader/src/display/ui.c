/*
 * SPDX-License-Identifier: MIT
 *
 * Status-bus listener that drives the display. The listener runs in the
 * publisher's context (often the USB MSC thread mid-transfer), so it only
 * copies the status and schedules a redraw on a low-priority thread.
 * State changes redraw immediately; progress redraws are throttled.
 * Only changed panel lines are sent, so a progress tick costs a few ms.
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "../uf2_status.h"
#include "lpm_panel.h"
#include "ui_render.h"

LOG_MODULE_REGISTER(ui, LOG_LEVEL_INF);

#define PROGRESS_INTERVAL_MS 150

static K_THREAD_STACK_DEFINE(ui_stack, 2048);
static struct k_work_q ui_q;
static struct k_spinlock lock;
static struct uf2_status pending;
static enum uf2_state drawn_state = UF2_STATE_BOOTING;
static int64_t write_start_ms;
static bool started;
static K_MUTEX_DEFINE(panel_lock);

static void render_handler(struct k_work *work)
{
	struct ui_model m = {
		.badge = CONFIG_UF2_DISPLAY_BADGE,
	};
	k_spinlock_key_t key = k_spin_lock(&lock);

	m.st = pending;
	k_spin_unlock(&lock, key);

	ARG_UNUSED(work);
	if (m.st.state == UF2_STATE_WRITING) {
		int64_t ms = k_uptime_get() - write_start_ms;

		m.rate_kbps = ms > 200 ? (uint32_t)((uint64_t)m.st.bytes_written * 1000U / 1024U / ms) : 0;
	}

	k_mutex_lock(&panel_lock, K_FOREVER);
	if (lpm_panel_ready()) {
		ui_render(&m);
		(void)lpm_panel_flush();
		drawn_state = m.st.state;
	}
	k_mutex_unlock(&panel_lock);
}
static K_WORK_DELAYABLE_DEFINE(render_work, render_handler);

static void init_handler(struct k_work *work)
{
	ARG_UNUSED(work);
	k_mutex_lock(&panel_lock, K_FOREVER);
	int rc = lpm_panel_init();

	k_mutex_unlock(&panel_lock);
	if (rc) {
		LOG_ERR("display init: %d", rc);
		return;
	}
	k_work_reschedule_for_queue(&ui_q, &render_work, K_NO_WAIT);
}
static K_WORK_DEFINE(init_work, init_handler);

void ui_start(void)
{
	k_work_queue_start(&ui_q, ui_stack, K_THREAD_STACK_SIZEOF(ui_stack),
			   K_LOWEST_APPLICATION_THREAD_PRIO, NULL);
	k_thread_name_set(&ui_q.thread, "ui");
	started = true;
	k_work_submit_to_queue(&ui_q, &init_work);
}

static void on_status(const struct uf2_status *st)
{
	if (!started) {
		return;
	}
	if (st->state == UF2_STATE_REBOOTING) {
		/* Datasheet off sequence before the app takes over (or doesn't). */
		k_work_cancel_delayable(&render_work);
		k_mutex_lock(&panel_lock, K_FOREVER);
		lpm_panel_off();
		k_mutex_unlock(&panel_lock);
		return;
	}

	k_spinlock_key_t key = k_spin_lock(&lock);
	bool state_changed = st->state != pending.state;

	if (state_changed && st->state == UF2_STATE_WRITING) {
		write_start_ms = k_uptime_get();
	}
	pending = *st;
	k_spin_unlock(&lock, key);

	if (state_changed || st->state != drawn_state) {
		k_work_reschedule_for_queue(&ui_q, &render_work, K_NO_WAIT);
	} else {
		/* Progress: coalesce into at most one redraw per interval. */
		k_work_schedule_for_queue(&ui_q, &render_work, K_MSEC(PROGRESS_INTERVAL_MS));
	}
}

UF2_STATUS_LISTENER_DEFINE(display_status, on_status);
