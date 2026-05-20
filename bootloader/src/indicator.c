/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * PWM LED indicator. Drives `pwm-led0`:
 *
 *   UNPLUGGED   slow triangle breathe (~1.5 s)
 *   PLUGGED     faster triangle breathe (~0.5 s)
 *   WRITING     fast hard blink
 *   FINISHED    solid on
 *
 * No-op if `pwm-led0` isn't defined on the board.
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/pwm.h>
#include <zephyr/logging/log.h>

#include "indicator.h"

LOG_MODULE_REGISTER(indicator, LOG_LEVEL_INF);

#define PWM_LED_NODE DT_ALIAS(pwm_led0)

#if DT_NODE_EXISTS(PWM_LED_NODE)
#define HAS_PWM_LED 1
static const struct pwm_dt_spec led_pwm = PWM_DT_SPEC_GET(PWM_LED_NODE);
#else
#define HAS_PWM_LED 0
#endif

#define TICK_MS                 20U
#define PERIOD_NS               1000000U
#define UNPLUGGED_PERIOD_TICKS  75U
#define PLUGGED_PERIOD_TICKS    25U

static volatile enum indicator_state current_state = INDICATOR_UNPLUGGED;
static uint16_t tick;

static uint8_t triangle(uint16_t t, uint16_t period_ticks)
{
    uint16_t phase = ((uint32_t)t * 256U / period_ticks) & 0xFFU;
    return phase < 128 ? (uint8_t)(phase * 2) : (uint8_t)((255 - phase) * 2);
}

static uint8_t compute_duty(enum indicator_state state, uint16_t t)
{
    switch (state) {
    case INDICATOR_UNPLUGGED: return triangle(t, UNPLUGGED_PERIOD_TICKS);
    case INDICATOR_PLUGGED:   return triangle(t, PLUGGED_PERIOD_TICKS);
    case INDICATOR_WRITING:   return (t & 0x3) < 2 ? 200 : 0;
    case INDICATOR_FINISHED:  return 200;
    default:                  return 0;
    }
}

static void apply_duty(uint8_t duty)
{
#if HAS_PWM_LED
    uint32_t pulse_ns = ((uint64_t)duty * PERIOD_NS) / 255U;
    int rc = pwm_set_dt(&led_pwm, PERIOD_NS, pulse_ns);
    if (rc) {
        LOG_WRN("pwm_set: %d", rc);
    }
#else
    ARG_UNUSED(duty);
#endif
}

static void tick_handler(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(tick_work, tick_handler);

static void tick_handler(struct k_work *work)
{
    ARG_UNUSED(work);
    tick++;
    apply_duty(compute_duty(current_state, tick));
    k_work_schedule(&tick_work, K_MSEC(TICK_MS));
}

void indicator_set(enum indicator_state state)
{
    current_state = state;
#if HAS_PWM_LED
    if (!device_is_ready(led_pwm.dev)) {
        LOG_WRN("pwm-led0 device not ready");
        return;
    }
    if (!k_work_delayable_is_pending(&tick_work)) {
        k_work_schedule(&tick_work, K_NO_WAIT);
    }
#endif
}
