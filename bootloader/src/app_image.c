/*
 * SPDX-License-Identifier: MIT
 *
 * Validation of and chain-load into slot0. Only reached on a boot where
 * USB was never started: after a DFU transfer the bootloader reboots
 * instead of jumping, so the application always inherits a clean chip.
 */

#include <cmsis_core.h>
#include <zephyr/cache.h>
#include <zephyr/devicetree.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "app_image.h"

LOG_MODULE_REGISTER(app_image, LOG_LEVEL_INF);

#define SRAM_START DT_REG_ADDR(DT_CHOSEN(zephyr_sram))
#define SRAM_END   (SRAM_START + DT_REG_SIZE(DT_CHOSEN(zephyr_sram)))

bool app_image_valid(void)
{
	const volatile uint32_t *vt = (const volatile uint32_t *)APP_IMAGE_START;
	uint32_t sp = vt[0];
	uint32_t pc = vt[1];
	uint32_t pc_addr = pc & ~1U;

	if (sp == 0xFFFFFFFFU && pc == 0xFFFFFFFFU) {
		return false; /* blank */
	}
	if (sp <= SRAM_START || sp > SRAM_END || (sp & 0x7U) != 0U) {
		LOG_WRN("app SP 0x%08x outside SRAM", sp);
		return false;
	}
	if ((pc & 1U) == 0U || pc_addr < APP_IMAGE_START || pc_addr >= APP_IMAGE_END) {
		LOG_WRN("app reset vector 0x%08x outside slot0", pc);
		return false;
	}
	return true;
}

FUNC_NORETURN void app_image_boot(void)
{
	const uint32_t *vt = (const uint32_t *)APP_IMAGE_START;
	uint32_t sp = vt[0];
	uint32_t pc = vt[1];

	LOG_INF("booting app at 0x%08x", (uint32_t)APP_IMAGE_START);
	/* Give the minimal log backend a moment to drain. */
	k_busy_wait(1000);

	__disable_irq();

	for (size_t i = 0; i < ARRAY_SIZE(NVIC->ICER); i++) {
		NVIC->ICER[i] = 0xFFFFFFFFU;
		NVIC->ICPR[i] = 0xFFFFFFFFU;
	}

	SysTick->CTRL = 0;
	SysTick->VAL = 0;
	SCB->ICSR = SCB_ICSR_PENDSTCLR_Msk | SCB_ICSR_PENDSVCLR_Msk;
	SCB->SHCSR &= ~(SCB_SHCSR_USGFAULTENA_Msk | SCB_SHCSR_BUSFAULTENA_Msk |
			SCB_SHCSR_MEMFAULTENA_Msk | SCB_SHCSR_SECUREFAULTENA_Msk);

	(void)sys_cache_instr_invd_all();
	(void)sys_cache_data_invd_all();

	SCB->VTOR = APP_IMAGE_START;

	/* Zephyr armed stack limits for its own stacks; the app sets its own. */
	__set_PSPLIM(0);
	__set_MSPLIM(0);

	/*
	 * We are on the main thread's PSP. Everything from the stack switch on
	 * stays in one asm block with sp/pc already in registers: no
	 * compiler-generated stack access is valid once CONTROL changes.
	 * End state matches reset: privileged, MSP, no FP context, PSP = 0.
	 */
	__asm__ volatile(
		"movs r2, #0\n"
		"msr control, r2\n"
		"isb\n"
		"msr psp, r2\n"
		"msr msp, %[sp]\n"
		"dsb\n"
		"isb\n"
		"cpsie i\n"
		"bx %[pc]\n"
		:
		: [sp] "r"(sp), [pc] "r"(pc)
		: "r2", "memory");
	CODE_UNREACHABLE;
}
