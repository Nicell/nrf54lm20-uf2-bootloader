/*
 * SPDX-License-Identifier: MIT
 *
 * Stop VREGUSB as early as possible on every boot.
 *
 * VREGUSB is not reset by a soft reset (sys_reboot, J-Link reset, ZMK's
 * &bootloader). If the previous firmware left it started with VBUS
 * present, the Zephyr driver's TASKS_START on this boot produces no new
 * VBUSDETECTED event, so the USBHS driver never gets "VBUS ready" and
 * USB never enumerates. Found on Lily Ultra hardware: after a J-Link
 * reset the bootloader sat in "waiting for USB" with 5.2 V on VBUS;
 * STOP then START over SWD raised the event immediately.
 *
 * Stopping it here makes the bootloader's own USB start see a fresh
 * edge, and hands the application a stopped regulator when we jump.
 */

#include <zephyr/devicetree.h>
#include <zephyr/init.h>

#if DT_NODE_HAS_STATUS_OKAY(DT_NODELABEL(vregusb))
#include <hal/nrf_vregusb.h>

#define VREGUSB ((NRF_VREGUSB_Type *)DT_REG_ADDR(DT_NODELABEL(vregusb)))

static int vregusb_reset(void)
{
	nrf_vregusb_int_disable(VREGUSB, NRF_VREGUSB_INT_VBUS_DETECTED_MASK |
					  NRF_VREGUSB_INT_VBUS_REMOVED_MASK);
	nrf_vregusb_task_trigger(VREGUSB, NRF_VREGUSB_TASK_STOP);
	nrf_vregusb_event_clear(VREGUSB, NRF_VREGUSB_EVENT_VBUS_DETECTED);
	nrf_vregusb_event_clear(VREGUSB, NRF_VREGUSB_EVENT_VBUS_REMOVED);
	return 0;
}

SYS_INIT(vregusb_reset, PRE_KERNEL_1, 0);
#endif
