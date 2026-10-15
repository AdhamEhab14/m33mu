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

/* Target for scripts/gdb-harness-test.py (STM32H563, secure).
 *
 * Prints "BOOT" on USART3. If sector 100 of bank 1 starts with its marker,
 * prints "UPDATED" and sleeps. Otherwise waits for one received byte and
 * echoes it as "GOT <c>", erases the sector and programs the marker
 * quad-word at its start, prints "DONE" and resets itself. */

#include <stdint.h>

#define REG(a) (*(volatile uint32_t *)(a))

#define RCC_APB1LENR   REG(0x44020C9Cu)
#define USART3         0x40004800u
#define USART_CR1      REG(USART3 + 0x00u)
#define USART_BRR      REG(USART3 + 0x0Cu)
#define USART_ISR      REG(USART3 + 0x1Cu)
#define USART_RDR      REG(USART3 + 0x24u)
#define USART_TDR      REG(USART3 + 0x28u)

#define FLASH_SECKEYR  REG(0x40022008u)
#define FLASH_SECCR    REG(0x4002202Cu)
#define FLASH_KEY1     0x45670123u
#define FLASH_KEY2     0xCDEF89ABu
#define CR_PG          (1u << 1)
#define CR_SER         (1u << 2)
#define CR_STRT        (1u << 5)
#define CR_SNB_SHIFT   6

#define AIRCR          REG(0xE000ED0Cu)

#define SECTOR         100u
#define SECTOR_ADDR    (0x0C000000u + SECTOR * 0x2000u)

extern unsigned long _estack;

static void putc_(char c)
{
    while ((USART_ISR & (1u << 7)) == 0u) { }
    USART_TDR = (uint32_t)(unsigned char)c;
}

static void puts_(const char *s)
{
    while (*s != '\0') putc_(*s++);
}

static void default_handler(void)
{
    while (1) { }
}

void Reset_Handler(void)
{
    volatile uint32_t *dst = (volatile uint32_t *)SECTOR_ADDR;
    char c;

    RCC_APB1LENR |= (1u << 18);
    USART_BRR = 64000000u / 115200u;
    USART_CR1 = (1u << 0) | (1u << 2) | (1u << 3);     /* UE, RE, TE */
    puts_("BOOT\n");
    if (dst[0] == 0x11111111u) {
        puts_("UPDATED\n");
        while (1) {
            __asm volatile ("wfi");
        }
    }

    while ((USART_ISR & (1u << 5)) == 0u) { }           /* RXNE */
    c = (char)USART_RDR;
    puts_("GOT ");
    putc_(c);
    putc_('\n');

    FLASH_SECKEYR = FLASH_KEY1;
    FLASH_SECKEYR = FLASH_KEY2;
    FLASH_SECCR = CR_SER | CR_STRT | (SECTOR << CR_SNB_SHIFT);
    FLASH_SECCR = CR_PG;
    dst[0] = 0x11111111u;
    dst[1] = 0x22222222u;
    dst[2] = 0x33333333u;
    dst[3] = 0x44444444u;
    FLASH_SECCR = 0u;
    puts_("DONE\n");
    while ((USART_ISR & (1u << 6)) == 0u) { }           /* TC: all sent */
    AIRCR = 0x05FA0004u;                                /* SYSRESETREQ */
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
    [15] = (uint32_t)&default_handler,
};
