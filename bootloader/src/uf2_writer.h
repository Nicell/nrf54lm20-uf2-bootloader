/*
 * SPDX-License-Identifier: MIT
 */

#ifndef UF2_WRITER_H
#define UF2_WRITER_H

#include "uf2.h"

/*
 * Handle one 512-byte sector written by the host. Non-UF2 sectors (FAT
 * metadata, macOS ._ files) and other families are ignored.
 */
void uf2_writer_sector(const UF2_Block *bl);

#endif
