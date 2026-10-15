/* m33mu -- an ARMv8-M Emulator
 *
 * Copyright (C) 2025  Daniele Lacamera <root@danielinux.net>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published
 * by the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 */

#ifndef M33MU_FLASH_OPS_H
#define M33MU_FLASH_OPS_H

#include "m33mu/types.h"

/* Flash operations, seen from outside the flash controller.
 *
 * Every port reports each erase or program it applies to the flash array
 * through mm_flash_persist_flush(); that call forwards here. So without any
 * port knowing about it, this layer counts the operations, logs them to a
 * trace file, and can cut the power just before a chosen one: the operation
 * is undone (the array goes back to what it held before) and the run stops.
 *
 * The kind of an operation is read from the array, against a shadow copy
 * that only operations update: a range left all 0xFF is an erase, anything
 * else a program. A rewrite of the whole array that is not an erase (a bank
 * swap) is a bulk operation. */

#define MM_FLASH_OPS_NEVER ((mm_u64)-1)

/* How long operations take on the chip, from its datasheet: [0] typical,
 * [1] the slowest allowed. A program of len bytes takes
 * ceil(len / program_unit) program times, an erase ceil(len / erase_unit)
 * erase times (bank and mass erases included, which datasheets give as
 * that sum). */
struct mm_flash_timing {
    mm_u32 program_unit;
    mm_u32 program_us[2];
    mm_u32 erase_unit;
    mm_u32 erase_us[2];
};

enum mm_flash_timing_level {
    MM_FLASH_TIMING_NONE = 0,
    MM_FLASH_TIMING_TYPICAL,
    MM_FLASH_TIMING_MAX
};

enum mm_flash_op_kind {
    MM_FLASH_OP_PROGRAM = 0,
    MM_FLASH_OP_ERASE,
    MM_FLASH_OP_MASS_ERASE,
    MM_FLASH_OP_BULK
};

struct mm_flash_cut {
    mm_u64 index;               /* the operation that did not happen */
    enum mm_flash_op_kind kind;
    mm_u32 addr;                /* bus address: flash base + offset */
    mm_u32 len;
};

/* Track the array from now on. `base` is the bus address offsets are named
 * by in the trace and cut reports. */
mm_bool mm_flash_ops_enable(mm_u8 *flash, mm_u32 size, mm_u32 base);
void mm_flash_ops_disable(void);
mm_bool mm_flash_ops_enabled(void);

/* The array was changed by something other than an operation (an image
 * load, a debugger write): take it as it is now. */
void mm_flash_ops_resync(void);

mm_u32 mm_flash_ops_base(void);
mm_u32 mm_flash_ops_size(void);

/* The whole array to or from a raw file, offset 0 first. A shorter file
 * loads into the start of the array and leaves the rest erased (0xFF). A
 * load is not an operation: it is taken as the array's new contents. */
mm_bool mm_flash_ops_load_file(const char *path);
mm_bool mm_flash_ops_save_file(const char *path);

/* Called for every operation, after the port applied it to the array.
 * Returns MM_FALSE when this is the operation to cut at, or any operation
 * after it (the power is off): the range is put back as it was. */
mm_bool mm_flash_ops_note(mm_u32 offset, mm_u32 len);

mm_u64 mm_flash_ops_count(void);
void mm_flash_ops_set_count(mm_u64 count);

/* Cut at operation `index` (counted from 0), or at the first erase that
 * covers `addr`. MM_FLASH_OPS_NEVER / mm_flash_ops_clear_cut() disarm. */
void mm_flash_ops_cut_at(mm_u64 index);
void mm_flash_ops_cut_on_erase(mm_u32 addr);
void mm_flash_ops_clear_cut(void);
mm_bool mm_flash_ops_cut_fired(struct mm_flash_cut *out);

/* One line per operation, appended to `path`:
 *   P <addr> <value>   program; value is the bytes written, little-endian,
 *                      two hex digits per byte
 *   E <addr> #<len>    erase of len bytes (hex)
 *   M <addr>           erase of the whole array
 *   B <addr> #<len>    bulk rewrite of the whole array (bank swap)
 * followed by @<us> (decimal) when operations take time. */
mm_bool mm_flash_ops_trace_open(const char *path);
void mm_flash_ops_trace_close(void);

const char *mm_flash_op_kind_name(enum mm_flash_op_kind kind);

/* Make operations take the time `timing` gives them (0 or NONE: no time).
 * The core cannot fetch from flash while it is busy, so the run loop stalls
 * the core for the time taken since it last asked; peripherals keep
 * counting. The trace gets a trailing @<us> on each line. */
void mm_flash_ops_set_timing(const struct mm_flash_timing *timing,
                             enum mm_flash_timing_level level);
mm_u64 mm_flash_ops_take_stall_us(void);

#endif /* M33MU_FLASH_OPS_H */
