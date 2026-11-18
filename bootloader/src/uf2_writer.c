/*
 * SPDX-License-Identifier: MIT
 *
 * UF2 transfer state machine.
 *
 * Crash safety: the block covering the app's first bytes (its vector
 * table) is held in RAM and written only after every other block has
 * landed. When a new transfer starts, the old vector table is wiped
 * first. So at any instant slot0 either has the old complete image,
 * no valid vector table (bootloader stays in DFU), or the new complete
 * image -- an unplug mid-copy can never boot half an image.
 */

#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/reboot.h>

#include "app_image.h"
#include "board_flash.h"
#include "uf2_status.h"
#include "uf2_writer.h"

LOG_MODULE_REGISTER(uf2_writer, LOG_LEVEL_INF);

#define UF2_FLAG_FILE_CONTAINER 0x00001000U
#define UF2_PAYLOAD_MAX 476U
#define VT_WIPE_LEN 16U

static struct {
	bool active;
	bool finished; /* committed or failed; ignore further blocks */
	bool failed;
	uint32_t num_blocks;
	uint32_t num_written;
	uint32_t bytes_written;
	bool have_vt;
	uint32_t vt_addr;
	uint32_t vt_len;
	uint8_t vt_data[UF2_PAYLOAD_MAX] __aligned(4);
	uint8_t readback[UF2_PAYLOAD_MAX];
	uint8_t mask[MAX_BLOCKS / 8 + 1];
} st;

static void reboot_handler(struct k_work *work)
{
	ARG_UNUSED(work);
	LOG_INF("rebooting into new image");
	sys_reboot(SYS_REBOOT_WARM);
}
static K_WORK_DELAYABLE_DEFINE(reboot_work, reboot_handler);

static void fail(enum uf2_error err)
{
	st.finished = true;
	st.failed = true;
	uf2_status_set_error(err);
}

static bool begin(uint32_t num_blocks)
{
	static const uint8_t blank[VT_WIPE_LEN] = {
		0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
		0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
	};

	(void)k_work_cancel_delayable(&reboot_work);
	memset(&st, 0, sizeof(st));
	st.active = true;
	st.num_blocks = num_blocks;

	LOG_INF("transfer start: %u blocks", num_blocks);
	uf2_status_set_state(UF2_STATE_WRITING);
	uf2_status_set_progress(0, num_blocks, 0);

	/* Invalidate the old image before touching any of it. */
	if (board_flash_write(APP_IMAGE_START, blank, sizeof(blank))) {
		fail(UF2_ERR_WRITE);
		return false;
	}
	return true;
}

static void commit(void)
{
	if (!st.have_vt) {
		fail(UF2_ERR_NO_VECTORS);
		return;
	}
	if (board_flash_write(st.vt_addr, st.vt_data, st.vt_len)) {
		fail(UF2_ERR_WRITE);
		return;
	}
	board_flash_read(st.vt_addr, st.readback, st.vt_len);
	if (memcmp(st.readback, st.vt_data, st.vt_len) != 0) {
		fail(UF2_ERR_VERIFY);
		return;
	}
	if (!app_image_valid()) {
		fail(UF2_ERR_INVALID_APP);
		return;
	}

	st.finished = true;
	LOG_INF("transfer complete: %u bytes", st.bytes_written);
	uf2_status_set_state(UF2_STATE_DONE);
	/* Let the host finish its trailing SCSI traffic before we vanish. */
	k_work_schedule(&reboot_work, K_MSEC(CONFIG_UF2_REBOOT_DELAY_MS));
}

void uf2_writer_sector(const UF2_Block *bl)
{
	const uint32_t align = board_flash_write_align();
	uint32_t bit, byte;
	int rc;

	if (bl->magicStart0 != UF2_MAGIC_START0 || bl->magicStart1 != UF2_MAGIC_START1 ||
	    bl->magicEnd != UF2_MAGIC_END) {
		return; /* not UF2: FAT/dir metadata or host junk files */
	}
	if ((bl->flags & UF2_FLAG_FILE_CONTAINER) || !(bl->flags & UF2_FLAG_FAMILYID) ||
	    bl->familyID != BOARD_UF2_FAMILY_ID) {
		return; /* someone else's image or a file-container UF2 */
	}

	/*
	 * After a failure, the user re-dragging the same file (same block
	 * count) shows up as block 0 again: start over. Otherwise a concluded
	 * transfer ignores stray rewrites until the reboot.
	 */
	bool retry = st.failed && bl->blockNo == 0U;

	if (st.active && bl->numBlocks == st.num_blocks && st.finished && !retry) {
		return;
	}
	if (!st.active || bl->numBlocks != st.num_blocks || retry) {
		if (bl->numBlocks == 0U) {
			return;
		}
		if (bl->numBlocks > MAX_BLOCKS) {
			memset(&st, 0, sizeof(st));
			st.active = true;
			st.num_blocks = bl->numBlocks;
			fail(UF2_ERR_TOO_BIG);
			return;
		}
		if (!begin(bl->numBlocks)) {
			return;
		}
	}

	if (bl->blockNo >= st.num_blocks || bl->payloadSize > UF2_PAYLOAD_MAX) {
		fail(UF2_ERR_BAD_BLOCK);
		return;
	}

	bit = BIT(bl->blockNo % 8U);
	byte = bl->blockNo / 8U;
	if (st.mask[byte] & bit) {
		return; /* duplicate; RRAM already holds it */
	}

	if (!(bl->flags & UF2_FLAG_NOFLASH) && bl->payloadSize > 0U) {
		if (!board_flash_in_slot(bl->targetAddr, bl->payloadSize)) {
			LOG_ERR("block %u: 0x%08x+%u outside slot0", bl->blockNo,
				bl->targetAddr, bl->payloadSize);
			fail(UF2_ERR_RANGE);
			return;
		}
		if ((bl->targetAddr % align) || (bl->payloadSize % align)) {
			fail(UF2_ERR_ALIGN);
			return;
		}

		if (bl->targetAddr == APP_IMAGE_START) {
			/* Vector table block: hold until everything else is down. */
			memcpy(st.vt_data, bl->data, bl->payloadSize);
			st.vt_addr = bl->targetAddr;
			st.vt_len = bl->payloadSize;
			st.have_vt = true;
		} else {
			rc = board_flash_write(bl->targetAddr, bl->data, bl->payloadSize);
			if (rc) {
				fail(UF2_ERR_WRITE);
				return;
			}
		}
		st.bytes_written += bl->payloadSize;
	}

	st.mask[byte] |= bit;
	st.num_written++;
	uf2_status_set_progress(st.num_written, st.num_blocks, st.bytes_written);

	if (st.num_written == st.num_blocks) {
		commit();
	}
}
