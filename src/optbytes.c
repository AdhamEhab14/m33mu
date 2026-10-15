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

#include <string.h>
#include "m33mu/optbytes.h"

#define OPTBYTES_MAX 16

static struct {
    const char *name;
    mm_optbyte_get_fn get;
    mm_optbyte_set_fn set;
} fields[OPTBYTES_MAX];
static size_t field_count;

static int find(const char *name)
{
    size_t i;
    if (name == 0) return -1;
    for (i = 0; i < field_count; ++i) {
        if (strcmp(fields[i].name, name) == 0) return (int)i;
    }
    return -1;
}

void mm_optbytes_register(const char *name, mm_optbyte_get_fn get, mm_optbyte_set_fn set)
{
    int i = find(name);
    if (name == 0 || get == 0 || set == 0) return;
    if (i < 0) {
        if (field_count >= OPTBYTES_MAX) return;
        i = (int)field_count++;
    }
    fields[i].name = name;
    fields[i].get = get;
    fields[i].set = set;
}

void mm_optbytes_clear(void)
{
    field_count = 0u;
}

size_t mm_optbytes_count(void)
{
    return field_count;
}

const char *mm_optbytes_name(size_t index)
{
    return (index < field_count) ? fields[index].name : 0;
}

mm_bool mm_optbytes_get(const char *name, mm_u32 *value_out)
{
    int i = find(name);
    if (i < 0 || value_out == 0) return MM_FALSE;
    *value_out = fields[i].get();
    return MM_TRUE;
}

mm_bool mm_optbytes_set(const char *name, mm_u32 value)
{
    int i = find(name);
    if (i < 0) return MM_FALSE;
    return fields[i].set(value);
}
