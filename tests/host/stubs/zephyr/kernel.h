#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#define BIT(n) (1UL << (n))
#define ARG_UNUSED(x) (void)(x)
#define __aligned(x) __attribute__((aligned(x)))
struct k_work { int unused; };
struct k_work_delayable { void (*handler)(struct k_work *); bool pending; };
#define K_WORK_DELAYABLE_DEFINE(name, fn) struct k_work_delayable name = { .handler = fn }
#define K_MSEC(ms) (ms)
/* The test reads this instead of the writer's static work item. */
extern bool test_work_pending;
static inline int k_work_schedule(struct k_work_delayable *w, int t) { (void)w; (void)t; test_work_pending = true; return 0; }
static inline int k_work_cancel_delayable(struct k_work_delayable *w) { (void)w; test_work_pending = false; return 0; }
static inline uint32_t k_cycle_get_32(void) { return 0; }
static inline uint64_t k_cyc_to_us_floor64(uint64_t c) { return c; }
