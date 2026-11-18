/*
 * SPDX-License-Identifier: MIT
 */

#ifndef UF2_BOOT_ENTRY_H
#define UF2_BOOT_ENTRY_H

#include "uf2_status.h"

/* Decide whether to stay in DFU. Returns UF2_ENTRY_NONE to boot the app. */
enum uf2_entry_reason boot_entry_check(void);

#endif
