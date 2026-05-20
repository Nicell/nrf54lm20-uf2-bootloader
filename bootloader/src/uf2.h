/*
 * UF2 block format and ghost-FAT API.
 * Ported from tinyuf2 (MIT, Microsoft + Adafruit + Henry Gabryjelski).
 */

#ifndef UF2FORMAT_H
#define UF2FORMAT_H 1

#include <stdint.h>
#include <stdbool.h>

#include "board_flash.h"

#ifndef CFG_UF2_FLASH_SIZE
#define CFG_UF2_FLASH_SIZE          CONFIG_UF2_FLASH_SIZE
#endif

#ifndef CFG_UF2_NUM_BLOCKS
#define CFG_UF2_NUM_BLOCKS          CONFIG_UF2_NUM_BLOCKS
#endif

#ifndef CFG_UF2_SECTORS_PER_CLUSTER
#define CFG_UF2_SECTORS_PER_CLUSTER (1)
#endif

#define UF2_MAGIC_START0    0x0A324655UL // "UF2\n"
#define UF2_MAGIC_START1    0x9E5D5157UL
#define UF2_MAGIC_END       0x0AB16F30UL

#define UF2_FLAG_NOFLASH    0x00000001
#define UF2_FLAG_FAMILYID   0x00002000

#define BOARD_UF2_FAMILY_ID CONFIG_UF2_FAMILY_ID

#define MAX_BLOCKS (CFG_UF2_FLASH_SIZE / 256 + 100)

typedef struct {
    uint32_t numBlocks;
    uint32_t numWritten;
    bool aborted;
    uint8_t writtenMask[MAX_BLOCKS / 8 + 1];
} WriteState;

typedef struct {
    uint32_t magicStart0;
    uint32_t magicStart1;
    uint32_t flags;
    uint32_t targetAddr;
    uint32_t payloadSize;
    uint32_t blockNo;
    uint32_t numBlocks;
    uint32_t familyID;
    uint8_t  data[476];
    uint32_t magicEnd;
} UF2_Block;

void uf2_init(void);
void uf2_read_block(uint32_t block_no, uint8_t *data);
int  uf2_write_block(uint32_t block_no, uint8_t *data, WriteState *state);

#endif
