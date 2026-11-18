/*
 * SPDX-License-Identifier: MIT
 *
 * Host test for src/uf2_writer.c against a simulated RRAM slot.
 * Run: make -C tests/host
 */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <zephyr/kernel.h>

#include "board_flash.h"
#include "uf2_status.h"
#include "uf2_writer.h"

/* ---- fakes ------------------------------------------------------------ */

static uint8_t rram[APP_IMAGE_SIZE];
static int writes;
static int fail_write_at = -1; /* inject a flash error on the Nth write */
static struct uf2_status st_now;
bool test_work_pending;

uint32_t board_flash_size(void) { return APP_IMAGE_SIZE; }
uint32_t board_flash_write_align(void) { return 16; }
bool board_flash_in_slot(uint32_t a, uint32_t l)
{
	return a >= APP_IMAGE_START && l <= APP_IMAGE_SIZE && a - APP_IMAGE_START <= APP_IMAGE_SIZE - l;
}
void board_flash_read(uint32_t a, void *b, uint32_t l) { memcpy(b, &rram[a - APP_IMAGE_START], l); }
int board_flash_write(uint32_t a, const void *d, uint32_t l)
{
	assert(board_flash_in_slot(a, l));
	assert(a % 16 == 0 && l % 16 == 0);
	if (writes++ == fail_write_at) {
		return -5;
	}
	memcpy(&rram[a - APP_IMAGE_START], d, l);
	return 0;
}
bool app_image_valid(void)
{
	uint32_t sp, pc;
	memcpy(&sp, &rram[0], 4);
	memcpy(&pc, &rram[4], 4);
	return sp > 0x20000000U && sp <= 0x20080000U && (pc & 1U) &&
	       (pc & ~1U) >= APP_IMAGE_START && (pc & ~1U) < APP_IMAGE_END;
}
void uf2_disk_request_eject(void) { test_work_pending = true; }
void uf2_disk_cancel_eject(void) { test_work_pending = false; }
void uf2_status_set_state(enum uf2_state s) { st_now.state = s; if (s == UF2_STATE_WRITING) st_now.error = 0; }
void uf2_status_set_error(enum uf2_error e) { st_now.error = e; st_now.state = UF2_STATE_ERROR; }
void uf2_status_set_progress(uint32_t w, uint32_t t, uint32_t b) { st_now.blocks_written = w; st_now.blocks_total = t; st_now.bytes_written = b; }

/* ---- image builder ---------------------------------------------------- */

#define PAYLOAD 256U
static uint8_t image[64 * 1024];
static size_t image_len;

static void make_image(size_t len, uint8_t seed)
{
	image_len = len;
	for (size_t i = 0; i < len; i++) {
		image[i] = (uint8_t)(i * 31 + seed);
	}
	uint32_t sp = 0x20040000U, pc = APP_IMAGE_START + 0x1234U + 1U;
	memcpy(&image[0], &sp, 4);
	memcpy(&image[4], &pc, 4);
}

static UF2_Block block(uint32_t n, uint32_t total)
{
	UF2_Block b = {0};
	b.magicStart0 = UF2_MAGIC_START0;
	b.magicStart1 = UF2_MAGIC_START1;
	b.magicEnd = UF2_MAGIC_END;
	b.flags = UF2_FLAG_FAMILYID;
	b.familyID = BOARD_UF2_FAMILY_ID;
	b.targetAddr = APP_IMAGE_START + n * PAYLOAD;
	b.payloadSize = PAYLOAD;
	b.blockNo = n;
	b.numBlocks = total;
	memcpy(b.data, &image[n * PAYLOAD], PAYLOAD);
	return b;
}

static uint32_t nblocks(void) { return (uint32_t)(image_len / PAYLOAD); }

static void send_range(uint32_t from, uint32_t to)
{
	for (uint32_t i = from; i < to; i++) {
		UF2_Block b = block(i, nblocks());
		uf2_writer_sector(&b);
	}
}

static bool image_matches(void) { return memcmp(rram, image, image_len) == 0; }

static void reset_world(void)
{
	memset(rram, 0xFF, sizeof(rram));
	memset(&st_now, 0, sizeof(st_now));
	writes = 0;
	fail_write_at = -1;
	test_work_pending = false;
}

/* Push the writer into a fresh transfer, then clear the fake world. */
static void fresh(void)
{
	UF2_Block b = {0};
	b.magicStart0 = UF2_MAGIC_START0;
	b.magicStart1 = UF2_MAGIC_START1;
	b.magicEnd = UF2_MAGIC_END;
	b.flags = UF2_FLAG_FAMILYID | UF2_FLAG_NOFLASH;
	b.familyID = BOARD_UF2_FAMILY_ID;
	b.numBlocks = 999;
	uf2_writer_sector(&b);
	reset_world();
}

#define CHECK(cond) do { if (!(cond)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); exit(1); } } while (0)
#define TEST(name) static void name(void)

/* ---- tests ------------------------------------------------------------ */

TEST(full_transfer_commits_and_reboots)
{
	make_image(16 * 1024, 1);
	send_range(0, nblocks());
	CHECK(image_matches());
	CHECK(st_now.state == UF2_STATE_DONE);
	CHECK(st_now.blocks_written == nblocks());
	CHECK(test_work_pending);
}

TEST(vector_table_written_last)
{
	make_image(16 * 1024, 2);
	send_range(0, nblocks() - 1);
	CHECK(!app_image_valid()); /* everything but the last block: still invalid */
	send_range(nblocks() - 1, nblocks());
	CHECK(app_image_valid());
}

TEST(interrupted_update_of_valid_app_leaves_it_unbootable)
{
	make_image(16 * 1024, 3); /* old app */
	send_range(0, nblocks());
	CHECK(app_image_valid());
	test_work_pending = false;

	make_image(20 * 1024, 4); /* new, different size: new transfer */
	send_range(0, nblocks() / 2); /* user unplugs halfway */
	CHECK(!app_image_valid());
	CHECK(st_now.state == UF2_STATE_WRITING);
	CHECK(!test_work_pending);
}

TEST(out_of_order_and_duplicate_blocks)
{
	make_image(16 * 1024, 5);
	for (int i = (int)nblocks() - 1; i >= 0; i -= 2) send_range(i, i + 1);
	send_range(3, 4);
	send_range(3, 4); /* duplicate */
	for (int i = (int)nblocks() - 2; i >= 0; i -= 2) send_range(i, i + 1);
	CHECK(image_matches());
	CHECK(st_now.state == UF2_STATE_DONE);
}

TEST(non_uf2_and_foreign_family_ignored)
{
	make_image(16 * 1024, 6);
	uint8_t junk[512] = "\x00\x00\x00 macOS ._ metadata";
	uf2_writer_sector((const UF2_Block *)junk);
	UF2_Block b = block(0, nblocks());
	b.familyID = 0x12345678;
	uf2_writer_sector(&b);
	CHECK(st_now.state == 0 && writes == 0);
	send_range(0, nblocks());
	CHECK(st_now.state == UF2_STATE_DONE);
}

TEST(out_of_range_rejected_without_touching_bootloader)
{
	make_image(16 * 1024, 7);
	UF2_Block b = block(1, nblocks());
	b.targetAddr = 0x1000; /* bootloader region */
	uf2_writer_sector(&b);
	CHECK(st_now.state == UF2_STATE_ERROR && st_now.error == UF2_ERR_RANGE);

	b = block(1, nblocks());
	b.targetAddr = APP_IMAGE_END - 128; /* runs past the end */
	fresh();
	uf2_writer_sector(&b);
	CHECK(st_now.error == UF2_ERR_RANGE);
}

TEST(malformed_blocks_rejected)
{
	make_image(16 * 1024, 8);
	UF2_Block b = block(1, nblocks());
	b.payloadSize = 480;
	uf2_writer_sector(&b);
	CHECK(st_now.error == UF2_ERR_BAD_BLOCK);

	fresh();
	b = block(1, nblocks());
	b.blockNo = nblocks();
	uf2_writer_sector(&b);
	CHECK(st_now.error == UF2_ERR_BAD_BLOCK);

	fresh();
	b = block(1, nblocks());
	b.payloadSize = 200; /* not 16-byte aligned */
	uf2_writer_sector(&b);
	CHECK(st_now.error == UF2_ERR_ALIGN);

	fresh();
	b = block(1, 0xFFFFFF);
	uf2_writer_sector(&b);
	CHECK(st_now.error == UF2_ERR_TOO_BIG);
}

TEST(image_without_vector_block_fails)
{
	make_image(16 * 1024, 9);
	UF2_Block b;
	for (uint32_t i = 0; i < nblocks(); i++) {
		b = block(i, nblocks());
		if (i == 0) b.flags |= UF2_FLAG_NOFLASH; /* counted, never written */
		uf2_writer_sector(&b);
	}
	CHECK(st_now.error == UF2_ERR_NO_VECTORS);
	CHECK(!test_work_pending);
}

TEST(invalid_vector_table_fails)
{
	make_image(16 * 1024, 10);
	memset(image, 0, 8);
	send_range(0, nblocks());
	CHECK(st_now.error == UF2_ERR_INVALID_APP);
	CHECK(!test_work_pending);
}

TEST(flash_error_then_redrag_same_file_succeeds)
{
	make_image(16 * 1024, 11);
	fail_write_at = 10;
	send_range(0, nblocks());
	CHECK(st_now.error == UF2_ERR_WRITE);
	CHECK(!app_image_valid());

	fail_write_at = -1;
	send_range(0, nblocks()); /* same numBlocks; restarts at block 0 */
	CHECK(st_now.state == UF2_STATE_DONE);
	CHECK(image_matches());
}

TEST(rewrites_after_success_are_ignored)
{
	make_image(16 * 1024, 12);
	send_range(0, nblocks());
	int w = writes;
	send_range(0, 3); /* host rewrites a few sectors before reboot */
	CHECK(writes == w);
	CHECK(st_now.state == UF2_STATE_DONE && app_image_valid());
}

int main(void)
{
	struct { const char *name; void (*fn)(void); } tests[] = {
#define T(x) { #x, x }
		T(full_transfer_commits_and_reboots),
		T(vector_table_written_last),
		T(interrupted_update_of_valid_app_leaves_it_unbootable),
		T(out_of_order_and_duplicate_blocks),
		T(non_uf2_and_foreign_family_ignored),
		T(out_of_range_rejected_without_touching_bootloader),
		T(malformed_blocks_rejected),
		T(image_without_vector_block_fails),
		T(invalid_vector_table_fails),
		T(flash_error_then_redrag_same_file_succeeds),
		T(rewrites_after_success_are_ignored),
	};
	for (size_t i = 0; i < sizeof(tests) / sizeof(tests[0]); i++) {
		fresh();
		tests[i].fn();
		printf("ok   %s\n", tests[i].name);
	}
	printf("all %zu tests passed\n", sizeof(tests) / sizeof(tests[0]));
	return 0;
}
