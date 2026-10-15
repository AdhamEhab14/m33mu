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

#ifndef M33MU_UART_CAPTURE_H
#define M33MU_UART_CAPTURE_H

#include <stddef.h>
#include "m33mu/types.h"

/* A UART whose bytes stay inside the emulator, for a debugger-driven test
 * harness (GDB `monitor uart ...`): what the guest transmits is kept until
 * taken, what the harness injects is received by the guest. Attach before
 * the guest enables the UART. Every byte the guest sends is kept, up to
 * MM_UART_CAPTURE_MAX not yet taken; beyond that the newest are dropped
 * and counted. */

#define MM_UART_CAPTURE_MAX (1u << 20)

mm_bool mm_uart_capture_attach(mm_u32 base);
void mm_uart_capture_detach_all(void);
/* Move up to `cap` captured bytes into `out`; returns how many. */
size_t mm_uart_capture_take(mm_u32 base, mm_u8 *out, size_t cap);
size_t mm_uart_capture_pending(mm_u32 base);
mm_u64 mm_uart_capture_dropped(mm_u32 base);
mm_bool mm_uart_capture_inject(mm_u32 base, const mm_u8 *data, size_t len);

#endif /* M33MU_UART_CAPTURE_H */
