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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "m33mu/flash_ops.h"

/* A program writes a word or a quad-word; a ROM API program can write a
 * page, but never leaves it all 0xFF in a range this long while the array
 * held something else. Shorter all-0xFF ranges are programs of 0xFF. */
#define MIN_ERASE_LEN 32u

static struct {
    mm_bool enabled;
    mm_u8 *flash;
    mm_u8 *shadow;
    mm_u32 size;
    mm_u32 base;
    mm_u64 count;
    mm_u64 cut_index;
    mm_bool cut_on_erase;
    mm_u32 cut_erase_off;
    mm_bool cut_fired;
    struct mm_flash_cut cut;
    FILE *trace;
} ops = { MM_FALSE, 0, 0, 0u, 0u, 0u, MM_FLASH_OPS_NEVER, MM_FALSE, 0u, MM_FALSE,
          { 0u, MM_FLASH_OP_PROGRAM, 0u, 0u }, 0 };

mm_bool mm_flash_ops_enable(mm_u8 *flash, mm_u32 size, mm_u32 base)
{
    mm_u8 *shadow;
    if (flash == 0 || size == 0u) return MM_FALSE;
    shadow = (mm_u8 *)malloc(size);
    if (shadow == 0) return MM_FALSE;
    mm_flash_ops_disable();
    memcpy(shadow, flash, size);
    ops.flash = flash;
    ops.shadow = shadow;
    ops.size = size;
    ops.base = base;
    ops.count = 0u;
    ops.enabled = MM_TRUE;
    mm_flash_ops_clear_cut();
    return MM_TRUE;
}

void mm_flash_ops_disable(void)
{
    free(ops.shadow);
    ops.shadow = 0;
    ops.flash = 0;
    ops.size = 0u;
    ops.enabled = MM_FALSE;
}

mm_bool mm_flash_ops_enabled(void)
{
    return ops.enabled;
}

void mm_flash_ops_resync(void)
{
    if (!ops.enabled) return;
    memcpy(ops.shadow, ops.flash, ops.size);
}

static mm_bool all_erased(const mm_u8 *p, mm_u32 len)
{
    mm_u32 i;
    for (i = 0; i < len; ++i) {
        if (p[i] != 0xFFu) return MM_FALSE;
    }
    return MM_TRUE;
}

static enum mm_flash_op_kind classify(mm_u32 offset, mm_u32 len)
{
    mm_bool erased = all_erased(ops.flash + offset, len);
    if (offset == 0u && len == ops.size) {
        return erased ? MM_FLASH_OP_MASS_ERASE : MM_FLASH_OP_BULK;
    }
    if (erased && len >= MIN_ERASE_LEN) {
        return MM_FLASH_OP_ERASE;
    }
    return MM_FLASH_OP_PROGRAM;
}

static void trace_op(enum mm_flash_op_kind kind, mm_u32 offset, mm_u32 len)
{
    mm_u32 addr = ops.base + offset;
    mm_u32 i;
    if (ops.trace == 0) return;
    switch (kind) {
    case MM_FLASH_OP_PROGRAM:
        fprintf(ops.trace, "P 0x%08lX 0x", (unsigned long)addr);
        for (i = len; i > 0u; --i) {
            fprintf(ops.trace, "%02X", ops.flash[offset + i - 1u]);
        }
        fputc('\n', ops.trace);
        break;
    case MM_FLASH_OP_ERASE:
        fprintf(ops.trace, "E 0x%08lX #%lX\n", (unsigned long)addr, (unsigned long)len);
        break;
    case MM_FLASH_OP_MASS_ERASE:
        fprintf(ops.trace, "M 0x%08lX\n", (unsigned long)addr);
        break;
    case MM_FLASH_OP_BULK:
        fprintf(ops.trace, "B 0x%08lX #%lX\n", (unsigned long)addr, (unsigned long)len);
        break;
    }
    fflush(ops.trace);
}

static mm_bool should_cut(enum mm_flash_op_kind kind, mm_u32 offset, mm_u32 len)
{
    if (ops.cut_fired) return MM_FALSE;
    if (ops.count == ops.cut_index) return MM_TRUE;
    if (ops.cut_on_erase && (kind == MM_FLASH_OP_ERASE || kind == MM_FLASH_OP_MASS_ERASE) &&
        ops.cut_erase_off >= offset && ops.cut_erase_off - offset < len) {
        return MM_TRUE;
    }
    return MM_FALSE;
}

mm_bool mm_flash_ops_note(mm_u32 offset, mm_u32 len)
{
    enum mm_flash_op_kind kind;
    if (!ops.enabled || len == 0u || offset >= ops.size) return MM_TRUE;
    if (len > ops.size - offset) len = ops.size - offset;
    kind = classify(offset, len);
    if (should_cut(kind, offset, len)) {
        /* Power fails just before this operation: it never happened. */
        memcpy(ops.flash + offset, ops.shadow + offset, len);
        ops.cut_fired = MM_TRUE;
        ops.cut.index = ops.count;
        ops.cut.kind = kind;
        ops.cut.addr = ops.base + offset;
        ops.cut.len = len;
        return MM_FALSE;
    }
    memcpy(ops.shadow + offset, ops.flash + offset, len);
    trace_op(kind, offset, len);
    ops.count++;
    return MM_TRUE;
}

mm_u64 mm_flash_ops_count(void)
{
    return ops.count;
}

void mm_flash_ops_set_count(mm_u64 count)
{
    ops.count = count;
}

void mm_flash_ops_cut_at(mm_u64 index)
{
    ops.cut_index = index;
}

void mm_flash_ops_cut_on_erase(mm_u32 addr)
{
    ops.cut_on_erase = MM_TRUE;
    ops.cut_erase_off = addr - ops.base;
}

void mm_flash_ops_clear_cut(void)
{
    ops.cut_index = MM_FLASH_OPS_NEVER;
    ops.cut_on_erase = MM_FALSE;
    ops.cut_fired = MM_FALSE;
}

mm_bool mm_flash_ops_cut_fired(struct mm_flash_cut *out)
{
    if (!ops.cut_fired) return MM_FALSE;
    if (out != 0) *out = ops.cut;
    return MM_TRUE;
}

mm_bool mm_flash_ops_trace_open(const char *path)
{
    FILE *f;
    if (path == 0) return MM_FALSE;
    f = fopen(path, "a");
    if (f == 0) return MM_FALSE;
    mm_flash_ops_trace_close();
    ops.trace = f;
    return MM_TRUE;
}

void mm_flash_ops_trace_close(void)
{
    if (ops.trace != 0) {
        fclose(ops.trace);
        ops.trace = 0;
    }
}

const char *mm_flash_op_kind_name(enum mm_flash_op_kind kind)
{
    switch (kind) {
    case MM_FLASH_OP_PROGRAM: return "program";
    case MM_FLASH_OP_ERASE: return "erase";
    case MM_FLASH_OP_MASS_ERASE: return "mass erase";
    case MM_FLASH_OP_BULK: return "bulk rewrite";
    }
    return "?";
}
