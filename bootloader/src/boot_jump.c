/*
 * SPDX-License-Identifier: MIT
 *
 * Cortex-M chain-load to the application via SCB->VTOR + MSP swap.
 * Reset vector format:
 *   app_base + 0x00 : initial MSP
 *   app_base + 0x04 : reset handler (Thumb bit set)
 */

#include <stdint.h>

#include <cmsis_core.h>
#include <zephyr/irq.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "boot_jump.h"

LOG_MODULE_REGISTER(boot_jump, LOG_LEVEL_INF);

void boot_jump_to_app(uint32_t app_base)
{
    const uint32_t *vt = (const uint32_t *)app_base;
    uint32_t sp = vt[0];
    uint32_t pc = vt[1];

    LOG_INF("chain-loading app at 0x%08x (sp=0x%08x pc=0x%08x)",
            app_base, sp, pc);

    /* irq_lock must come after logging — the LOG subsystem can't make
     * progress with IRQs disabled. */
    irq_lock();
    for (int i = 0; i < CONFIG_NUM_IRQS; i++) {
        irq_disable(i);
    }
    __DSB();
    __ISB();

    SCB->VTOR = app_base;
    __DSB();
    __ISB();

    __set_MSP(sp);
    ((void (*)(void))pc)();

    CODE_UNREACHABLE;
}
