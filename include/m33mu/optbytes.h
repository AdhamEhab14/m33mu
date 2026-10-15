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

#ifndef M33MU_OPTBYTES_H
#define M33MU_OPTBYTES_H

#include <stddef.h>
#include "m33mu/types.h"

/* Option bytes by name, outside the flash array.
 *
 * A target publishes the non-volatile option fields it models (for example
 * SWAP_BANK, WRPSG1) with a getter and a setter. Setting one has the effect
 * a completed option-byte change has on the chip. `--optbyte NAME=VALUE`
 * and the GDB `monitor optbytes` commands go through here, so a harness
 * can provision a device and save and restore its option state next to the
 * flash contents. Registering a name again replaces it. */

typedef mm_u32 (*mm_optbyte_get_fn)(void);
typedef mm_bool (*mm_optbyte_set_fn)(mm_u32 value);

void mm_optbytes_register(const char *name, mm_optbyte_get_fn get, mm_optbyte_set_fn set);
void mm_optbytes_clear(void);
size_t mm_optbytes_count(void);
const char *mm_optbytes_name(size_t index);
mm_bool mm_optbytes_get(const char *name, mm_u32 *value_out);
mm_bool mm_optbytes_set(const char *name, mm_u32 value);

#endif /* M33MU_OPTBYTES_H */
