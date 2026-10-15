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

/* LPC55S69 flash changed through the flash controller commands or through
 * the ROM API must reach the --persist file. */

#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "m33mu/mmio.h"
#include "m33mu/memmap.h"
#include "m33mu/cpu.h"
#include "m33mu/flash_persist.h"
#include "m33mu/target.h"
#include "lpc55s69/lpc55s69_mmio.h"
#include "lpc55s69/lpc55s69_romapi.h"
#include "lpc55s69/cpu_config.h"

#define FC_BASE_S        0x50034000u
#define FC_OFF_CMD       0x000u
#define FC_OFF_STARTA    0x010u
#define FC_OFF_STOPA     0x014u
#define FC_OFF_DATAW0    0x080u
#define FC_CMD_ERASE_PAGE   4u
#define FC_CMD_WRITE_PHRASE 8u

#define ROMAPI_STUB_BASE_S        0x13002000u
#define ROMAPI_STUB_FLASH_ERASE   (ROMAPI_STUB_BASE_S + 0x020u)
#define ROMAPI_STUB_FLASH_PROGRAM (ROMAPI_STUB_BASE_S + 0x030u)
#define ROMAPI_FLASH_ERASE_KEY    0x6b65666cu

#define BASE_S  LPC55S69_FLASH_BASE_S
#define SIZE    LPC55S69_FLASH_SIZE

static mm_u8 flash[SIZE];

static int expect(const char *path, long off, mm_u8 want, const char *what)
{
    FILE *f = fopen(path, "rb");
    int c = EOF;
    if (f != 0) {
        if (fseek(f, off, SEEK_SET) == 0) c = fgetc(f);
        fclose(f);
    }
    if (c != (int)want) {
        fprintf(stderr, "%s: file byte 0x%lx is 0x%x, want 0x%02x\n", what, off, c, want);
        return 1;
    }
    return 0;
}

static int romapi_call(struct mm_cpu *cpu, struct mm_memmap *map, mm_u32 stub,
                       mm_u32 r1, mm_u32 r2, mm_u32 r3)
{
    cpu->r[0] = 0u;
    cpu->r[1] = r1;
    cpu->r[2] = r2;
    cpu->r[3] = r3;
    cpu->r[14] = BASE_S + 0x201u;
    cpu->r[15] = stub;
    if (!mm_lpc55s69_romapi_handle(cpu, map)) return 1;
    return cpu->r[0] == 0u ? 0 : 1;
}

int main(void)
{
    struct mm_memmap map;
    struct mmio_region regions[128];
    struct mm_target_cfg cfg;
    struct mm_flash_persist persist;
    struct mm_cpu cpu;
    char path[] = "/tmp/m33mu_lpc55s69_persist_XXXXXX";
    const char *paths[1];
    mm_u32 offsets[1];
    int fd;
    int fails = 0;

    memset(flash, 0xFF, sizeof(flash));
    memset(flash + 0x3000u, 0xC3, 0x10u);   /* source for FLASH_Program */
    fd = mkstemp(path);
    if (fd < 0) return 1;
    close(fd);
    paths[0] = path;
    offsets[0] = 0u;
    memset(&persist, 0, sizeof(persist));
    mm_flash_persist_build(&persist, flash, SIZE, paths, offsets, 1);

    memset(&cfg, 0, sizeof(cfg));
    cfg.flash_base_s = BASE_S;
    cfg.flash_size_s = SIZE;
    cfg.flash_base_ns = LPC55S69_FLASH_BASE_NS;
    cfg.flash_size_ns = SIZE;
    mm_memmap_init(&map, regions, 128u);
    (void)mm_memmap_configure_flash(&map, &cfg, flash, MM_TRUE);
    map.flash_base_s = BASE_S;
    map.flash_size_s = SIZE;
    map.flash_base_ns = LPC55S69_FLASH_BASE_NS;
    map.flash_size_ns = SIZE;
    mm_lpc55s69_mmio_reset();
    (void)mm_lpc55s69_register_mmio(&map.mmio);
    mm_lpc55s69_flash_bind(&map, flash, SIZE, &persist, 0u);
    mmio_set_active_sec(MM_SECURE);

    /* flash controller: write one phrase at 0x1000, then erase its page */
    (void)mmio_bus_write(&map.mmio, FC_BASE_S + FC_OFF_STARTA, 4u, 0x1000u / 16u);
    (void)mmio_bus_write(&map.mmio, FC_BASE_S + FC_OFF_STOPA, 4u, 0x1000u / 16u);
    (void)mmio_bus_write(&map.mmio, FC_BASE_S + FC_OFF_DATAW0, 4u, 0x000000A5u);
    (void)mmio_bus_write(&map.mmio, FC_BASE_S + FC_OFF_CMD, 4u, FC_CMD_WRITE_PHRASE);
    fails += expect(path, 0x1000, 0xA5, "FC write phrase");
    (void)mmio_bus_write(&map.mmio, FC_BASE_S + FC_OFF_CMD, 4u, FC_CMD_ERASE_PAGE);
    fails += expect(path, 0x1000, 0xFF, "FC erase page");

    /* ROM API: program 16 bytes at 0x2000, then erase them */
    memset(&cpu, 0, sizeof(cpu));
    cpu.sec_state = MM_SECURE;
    if (romapi_call(&cpu, &map, ROMAPI_STUB_FLASH_PROGRAM, BASE_S + 0x2000u, BASE_S + 0x3000u, 16u) != 0) {
        fprintf(stderr, "FLASH_Program failed\n");
        fails++;
    }
    fails += expect(path, 0x2000, 0xC3, "FLASH_Program");
    if (romapi_call(&cpu, &map, ROMAPI_STUB_FLASH_ERASE, BASE_S + 0x2000u, 0x200u, ROMAPI_FLASH_ERASE_KEY) != 0) {
        fprintf(stderr, "FLASH_Erase failed\n");
        fails++;
    }
    fails += expect(path, 0x2000, 0xFF, "FLASH_Erase");

    mm_flash_persist_build(&persist, 0, 0, 0, 0, 0);
    unlink(path);
    if (fails != 0) {
        printf("lpc55s69_flash_persist_test: FAIL (%d)\n", fails);
        return 1;
    }
    printf("lpc55s69_flash_persist_test: PASS\n");
    return 0;
}
