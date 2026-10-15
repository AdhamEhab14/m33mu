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

/* Sleeps in WFI for 200 full SysTick periods (200 * 2^24 cycles, several
 * virtual seconds at any core clock), then hits BKPT 0x7f. Under
 * --no-realtime that wait costs next to no wall time. */

#include <stdint.h>

#define SYST_CSR (*(volatile uint32_t *)0xE000E010u)
#define SYST_RVR (*(volatile uint32_t *)0xE000E014u)
#define SYST_CVR (*(volatile uint32_t *)0xE000E018u)

#define WRAPS 200u

extern unsigned long _estack;

static volatile uint32_t ticks;

void SysTick_Handler(void)
{
    ticks++;
}

static void default_handler(void)
{
    while (1) { }
}

void Reset_Handler(void)
{
    ticks = 0u;                 /* no startup code: .bss is not zeroed */
    SYST_RVR = 0x00FFFFFFu;
    SYST_CVR = 0u;
    SYST_CSR = 0x7u;            /* processor clock, interrupt, enable */
    while (ticks < WRAPS) {
        __asm volatile ("wfi");
    }
    SYST_CSR = 0u;
    __asm volatile ("bkpt #0x7f");
    while (1) { }
}

__attribute__((section(".isr_vector")))
const uint32_t vector_table[16] = {
    [0] = (uint32_t)&_estack,
    [1] = (uint32_t)&Reset_Handler,
    [2] = (uint32_t)&default_handler,
    [3] = (uint32_t)&default_handler,
    [4] = (uint32_t)&default_handler,
    [5] = (uint32_t)&default_handler,
    [6] = (uint32_t)&default_handler,
    [11] = (uint32_t)&default_handler,
    [12] = (uint32_t)&default_handler,
    [14] = (uint32_t)&default_handler,
    [15] = (uint32_t)&SysTick_Handler,
};
