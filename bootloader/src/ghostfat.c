/*
 * SPDX-License-Identifier: MIT
 *
 * Ghost-FAT — synthesizes a FAT16 volume on the fly and exposes the
 * application flash region as a single CURRENT.UF2 file. Writes to
 * CURRENT.UF2 (i.e. UF2 blocks dropped onto the volume by the host) are
 * parsed and dispatched to board_flash_write().
 *
 * Ported from tinyuf2/src/ghostfat.c (MIT, Microsoft + Adafruit +
 * Henry Gabryjelski).
 */

#include <string.h>
#include <stdio.h>
#include <inttypes.h>

#include <zephyr/logging/log.h>

#include "compile_date.h"
#include "board_flash.h"
#include "uf2.h"

LOG_MODULE_REGISTER(ghostfat, LOG_LEVEL_INF);

#define TINYUF2_CONST

#define STATIC_ASSERT(_exp) _Static_assert(_exp, "static assert failed")

#define UF2_ARRAY_SIZE(_arr)    (sizeof(_arr) / sizeof(_arr[0]))
#define UF2_DIV_CEIL(_v, _d)    (((_v) / (_d)) + ((_v) % (_d) ? 1 : 0))

typedef struct {
    uint8_t  JumpInstruction[3];
    uint8_t  OEMInfo[8];
    uint16_t SectorSize;
    uint8_t  SectorsPerCluster;
    uint16_t ReservedSectors;
    uint8_t  FATCopies;
    uint16_t RootDirectoryEntries;
    uint16_t TotalSectors16;
    uint8_t  MediaDescriptor;
    uint16_t SectorsPerFAT;
    uint16_t SectorsPerTrack;
    uint16_t Heads;
    uint32_t HiddenSectors;
    uint32_t TotalSectors32;
    uint8_t  PhysicalDriveNum;
    uint8_t  Reserved;
    uint8_t  ExtendedBootSig;
    uint32_t VolumeSerialNumber;
    uint8_t  VolumeLabel[11];
    uint8_t  FilesystemIdentifier[8];
} __attribute__((packed)) FAT_BootBlock;

typedef struct {
    char     name[8];
    char     ext[3];
    uint8_t  attrs;
    uint8_t  reserved;
    uint8_t  createTimeFine;
    uint16_t createTime;
    uint16_t createDate;
    uint16_t lastAccessDate;
    uint16_t highStartCluster;
    uint16_t updateTime;
    uint16_t updateDate;
    uint16_t startCluster;
    uint32_t size;
} __attribute__((packed)) DirEntry;
STATIC_ASSERT(sizeof(DirEntry) == 32);

typedef struct FileContent {
    char const   name[11];
    void const  *content;
    uint32_t     size;

    uint16_t cluster_start;
    uint16_t cluster_end;
} FileContent_t;

static uint32_t _flash_size;

#define BPB_SECTOR_SIZE           (512)
#define BPB_SECTORS_PER_CLUSTER   (CFG_UF2_SECTORS_PER_CLUSTER)
#define BPB_RESERVED_SECTORS      (1)
#define BPB_NUMBER_OF_FATS        (2)
#define BPB_ROOT_DIR_ENTRIES      (64)
#define BPB_TOTAL_SECTORS         CFG_UF2_NUM_BLOCKS
#define BPB_MEDIA_DESCRIPTOR_BYTE (0xF8)
#define FAT_ENTRY_SIZE            (2)
#define FAT_ENTRIES_PER_SECTOR    (BPB_SECTOR_SIZE / FAT_ENTRY_SIZE)
#define FAT_END_OF_CHAIN          (0xFFFF)

#define TOTAL_CLUSTERS_ROUND_UP   UF2_DIV_CEIL(BPB_TOTAL_SECTORS, BPB_SECTORS_PER_CLUSTER)
#define BPB_SECTORS_PER_FAT       UF2_DIV_CEIL(TOTAL_CLUSTERS_ROUND_UP, FAT_ENTRIES_PER_SECTOR)
#define DIRENTRIES_PER_SECTOR     (BPB_SECTOR_SIZE/sizeof(DirEntry))
#define ROOT_DIR_SECTOR_COUNT     UF2_DIV_CEIL(BPB_ROOT_DIR_ENTRIES, DIRENTRIES_PER_SECTOR)
#define BPB_BYTES_PER_CLUSTER     (BPB_SECTOR_SIZE * BPB_SECTORS_PER_CLUSTER)

STATIC_ASSERT((BPB_SECTORS_PER_CLUSTER & (BPB_SECTORS_PER_CLUSTER-1)) == 0);
STATIC_ASSERT(BPB_SECTOR_SIZE                              ==       512);
STATIC_ASSERT(BPB_NUMBER_OF_FATS                           ==         2);
STATIC_ASSERT(sizeof(DirEntry)                             ==        32);
STATIC_ASSERT(BPB_SECTOR_SIZE % sizeof(DirEntry)           ==         0);
STATIC_ASSERT(BPB_ROOT_DIR_ENTRIES % DIRENTRIES_PER_SECTOR ==         0);
STATIC_ASSERT(BPB_BYTES_PER_CLUSTER                        <= (32*1024));
STATIC_ASSERT(FAT_ENTRIES_PER_SECTOR                       ==       256);

#define UF2_FIRMWARE_BYTES_PER_SECTOR   256
#define UF2_SECTOR_COUNT                (_flash_size / UF2_FIRMWARE_BYTES_PER_SECTOR)
#define UF2_BYTE_COUNT                  (UF2_SECTOR_COUNT * BPB_SECTOR_SIZE)

static char infoUf2File[128*3] =
    "UF2 Bootloader v0.1\r\n"
    "Model: " CONFIG_UF2_PRODUCT_NAME "\r\n"
    "Board-ID: " CONFIG_UF2_BOARD_ID "\r\n"
    "Date: " COMPILE_DATE "\r\n"
    "Flash Size: 0x";

static TINYUF2_CONST char indexFile[] =
    "<!doctype html>\n"
    "<html><body><script>\n"
    "location.replace(\"" CONFIG_UF2_INDEX_URL "\");\n"
    "</script></body></html>\n";

static FileContent_t info[] = {
    {.name = "INFO_UF2TXT", .content = infoUf2File, .size = sizeof(infoUf2File) - 1},
    {.name = "INDEX   HTM", .content = indexFile,   .size = sizeof(indexFile) - 1},
    /* CURRENT.UF2 must be last and content must be NULL — generated on demand. */
    {.name = "CURRENT UF2", .content = NULL,        .size = 0},
};

enum {
    NUM_FILES = sizeof(info) / sizeof(info[0]),
    NUM_DIRENTRIES = NUM_FILES + 1,
};

enum {
    FID_INFO = 0,
    FID_INDEX = 1,
    FID_UF2 = NUM_FILES - 1,
};

STATIC_ASSERT(NUM_DIRENTRIES < BPB_ROOT_DIR_ENTRIES);
STATIC_ASSERT(NUM_DIRENTRIES < DIRENTRIES_PER_SECTOR);

#define NUM_SECTORS_IN_DATA_REGION (BPB_TOTAL_SECTORS - BPB_RESERVED_SECTORS - \
                                    (BPB_NUMBER_OF_FATS * BPB_SECTORS_PER_FAT) - \
                                    ROOT_DIR_SECTOR_COUNT)
#define CLUSTER_COUNT              (NUM_SECTORS_IN_DATA_REGION / BPB_SECTORS_PER_CLUSTER)

STATIC_ASSERT(CLUSTER_COUNT >= 0x0FF5 && CLUSTER_COUNT < 0xFFF5);
STATIC_ASSERT(CLUSTER_COUNT >= 0x1015 && CLUSTER_COUNT < 0xFFD5);

#define FS_START_FAT0_SECTOR      BPB_RESERVED_SECTORS
#define FS_START_FAT1_SECTOR      (FS_START_FAT0_SECTOR + BPB_SECTORS_PER_FAT)
#define FS_START_ROOTDIR_SECTOR   (FS_START_FAT1_SECTOR + BPB_SECTORS_PER_FAT)
#define FS_START_CLUSTERS_SECTOR  (FS_START_ROOTDIR_SECTOR + ROOT_DIR_SECTOR_COUNT)

static FAT_BootBlock TINYUF2_CONST BootBlock = {
    .JumpInstruction      = {0xeb, 0x3c, 0x90},
    .OEMInfo              = "UF2 UF2 ",
    .SectorSize           = BPB_SECTOR_SIZE,
    .SectorsPerCluster    = BPB_SECTORS_PER_CLUSTER,
    .ReservedSectors      = BPB_RESERVED_SECTORS,
    .FATCopies            = BPB_NUMBER_OF_FATS,
    .RootDirectoryEntries = BPB_ROOT_DIR_ENTRIES,
    .TotalSectors16       = (BPB_TOTAL_SECTORS > 0xFFFF) ? 0 : BPB_TOTAL_SECTORS,
    .MediaDescriptor      = BPB_MEDIA_DESCRIPTOR_BYTE,
    .SectorsPerFAT        = BPB_SECTORS_PER_FAT,
    .SectorsPerTrack      = 1,
    .Heads                = 1,
    .TotalSectors32       = (BPB_TOTAL_SECTORS > 0xFFFF) ? BPB_TOTAL_SECTORS : 0,
    .PhysicalDriveNum     = 0x80,
    .ExtendedBootSig      = 0x29,
    .VolumeSerialNumber   = 0x00420042,
    .VolumeLabel          = CONFIG_UF2_VOLUME_LABEL,
    .FilesystemIdentifier = "FAT16   ",
};

static void init_starting_clusters(void)
{
    uint16_t start_cluster = 2;

    for (uint16_t i = 0; i < NUM_FILES; i++) {
        info[i].cluster_start = start_cluster;
        info[i].cluster_end = start_cluster +
            UF2_DIV_CEIL(info[i].size, BPB_SECTOR_SIZE * BPB_SECTORS_PER_CLUSTER) - 1;
        start_cluster = info[i].cluster_end + 1;
    }
}

static uint32_t info_index_of(uint32_t cluster)
{
    if (cluster >= 0xFFF0) {
        return FID_UF2;
    }
    for (uint32_t i = 0; i < NUM_FILES; i++) {
        if ((info[i].cluster_start <= cluster) && (cluster <= info[i].cluster_end)) {
            return i;
        }
    }
    return FID_UF2;
}

static void u32_to_hexstr(uint32_t value, char *buffer)
{
    static const char hexDigits[] = "0123456789ABCDEF";
    size_t i;

    for (i = 0; i < 8; i++) {
        buffer[i] = hexDigits[(value >> (28 - i * 4)) & 0xF];
    }
    buffer[i] = '\0';
}

void uf2_init(void)
{
    _flash_size = board_flash_size();

    info[FID_UF2].size = UF2_BYTE_COUNT;

    size_t txt_len = strlen(infoUf2File);
    size_t const max_len = sizeof(infoUf2File) - 1;
    if (max_len - txt_len > 8) {
        u32_to_hexstr(_flash_size, infoUf2File + txt_len);
        txt_len += 8;
        if (max_len - txt_len > 6) {
            strcpy(infoUf2File + txt_len, " bytes");
            txt_len += 6;
        }
    }
    info[FID_INFO].size = txt_len;

    init_starting_clusters();

    LOG_INF("UF2 init: flash=%u total_sectors=%u sectors_per_fat=%u clusters=%u",
            _flash_size, BPB_TOTAL_SECTORS, BPB_SECTORS_PER_FAT, CLUSTER_COUNT);
}

static void padded_memcpy(char *dst, char const *src, int len)
{
    for (int i = 0; i < len; ++i) {
        *dst++ = *src ? *src++ : ' ';
        (void)0;
    }
}

void uf2_read_block(uint32_t block_no, uint8_t *data)
{
    memset(data, 0, BPB_SECTOR_SIZE);
    uint32_t sectionRelativeSector = block_no;

    if (block_no == 0) {
        memcpy(data, &BootBlock, sizeof(BootBlock));
        data[510] = 0x55;
        data[511] = 0xaa;
    } else if (block_no < FS_START_ROOTDIR_SECTOR) {
        sectionRelativeSector -= FS_START_FAT0_SECTOR;
        if (sectionRelativeSector >= BPB_SECTORS_PER_FAT) {
            sectionRelativeSector -= BPB_SECTORS_PER_FAT;
        }

        uint16_t *data16 = (uint16_t *)(void *)data;
        uint32_t sectorFirstCluster = sectionRelativeSector * FAT_ENTRIES_PER_SECTOR;
        uint32_t firstUnusedCluster = info[FID_UF2].cluster_end + 1;

        for (uint16_t i = 0; i < FAT_ENTRIES_PER_SECTOR; i++) {
            uint32_t cluster = i + sectorFirstCluster;
            data16[i] = (cluster >= firstUnusedCluster) ? 0 : cluster + 1;
        }

        if (sectionRelativeSector == 0) {
            data[0] = BPB_MEDIA_DESCRIPTOR_BYTE;
            data[1] = 0xff;
            data16[1] = FAT_END_OF_CHAIN;
        }

        for (uint32_t i = 0; i < NUM_FILES; i++) {
            uint32_t lastClusterOfFile = info[i].cluster_end;
            if (lastClusterOfFile >= sectorFirstCluster) {
                uint32_t idx = lastClusterOfFile - sectorFirstCluster;
                if (idx < FAT_ENTRIES_PER_SECTOR) {
                    data16[idx] = FAT_END_OF_CHAIN;
                }
            }
        }
    } else if (block_no < FS_START_CLUSTERS_SECTOR) {
        sectionRelativeSector -= FS_START_ROOTDIR_SECTOR;

        DirEntry *d = (void *)data;
        int remainingEntries = DIRENTRIES_PER_SECTOR;
        uint32_t startingFileIndex;

        if (sectionRelativeSector == 0) {
            padded_memcpy(d->name, (char const *)BootBlock.VolumeLabel, 11);
            d->attrs = 0x28;
            d++;
            remainingEntries--;
            startingFileIndex = 0;
        } else {
            startingFileIndex = DIRENTRIES_PER_SECTOR * sectionRelativeSector - 1;
        }

        for (uint32_t fileIndex = startingFileIndex;
             remainingEntries > 0 && fileIndex < NUM_FILES;
             fileIndex++, d++) {
            uint32_t const startCluster = info[fileIndex].cluster_start;
            FileContent_t const *inf = &info[fileIndex];

            padded_memcpy(d->name, inf->name, 11);
            d->createTimeFine   = COMPILE_SECONDS_INT % 2 * 100;
            d->createTime       = COMPILE_DOS_TIME;
            d->createDate       = COMPILE_DOS_DATE;
            d->lastAccessDate   = COMPILE_DOS_DATE;
            d->highStartCluster = startCluster >> 16;
            d->updateTime       = COMPILE_DOS_TIME;
            d->updateDate       = COMPILE_DOS_DATE;
            d->startCluster     = startCluster & 0xFFFF;
            d->size             = (inf->content ? inf->size : UF2_BYTE_COUNT);
        }
    } else if (block_no < BPB_TOTAL_SECTORS) {
        sectionRelativeSector -= FS_START_CLUSTERS_SECTOR;

        uint32_t fid = info_index_of(2 + sectionRelativeSector / BPB_SECTORS_PER_CLUSTER);
        FileContent_t const *inf = &info[fid];

        uint32_t fileRelativeSector = sectionRelativeSector -
            (info[fid].cluster_start - 2) * BPB_SECTORS_PER_CLUSTER;

        if (fid != FID_UF2) {
            size_t fileContentStartOffset = fileRelativeSector * BPB_SECTOR_SIZE;
            size_t fileContentLength = inf->size;

            if (fileContentLength > fileContentStartOffset) {
                const void *dataStart = ((const uint8_t *)inf->content) + fileContentStartOffset;
                size_t bytesToCopy = fileContentLength - fileContentStartOffset;
                if (bytesToCopy > BPB_SECTOR_SIZE) {
                    bytesToCopy = BPB_SECTOR_SIZE;
                }
                memcpy(data, dataStart, bytesToCopy);
            }
        } else {
            uint32_t addr = BOARD_FLASH_APP_START + (fileRelativeSector * UF2_FIRMWARE_BYTES_PER_SECTOR);
            if (addr < (BOARD_FLASH_APP_START + _flash_size)) {
                UF2_Block *bl = (void *)data;
                bl->magicStart0 = UF2_MAGIC_START0;
                bl->magicStart1 = UF2_MAGIC_START1;
                bl->magicEnd    = UF2_MAGIC_END;
                bl->blockNo     = fileRelativeSector;
                bl->numBlocks   = UF2_SECTOR_COUNT;
                bl->targetAddr  = addr;
                bl->payloadSize = UF2_FIRMWARE_BYTES_PER_SECTOR;
                bl->flags       = UF2_FLAG_FAMILYID;
                bl->familyID    = BOARD_UF2_FAMILY_ID;

                board_flash_read(addr, bl->data, bl->payloadSize);
            }
        }
    }
}
