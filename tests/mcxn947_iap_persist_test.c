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

/* Flash changed through the MCXN947 IAP memory interface (MEM_Write,
 * MEM_Fill, MEM_Erase, MEM_EraseAll) must reach the --persist file, as
 * changes through FLASH_Program and FLASH_Erase do. */

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
#include "mcxn947/mcxn947_mmio.h"
#include "mcxn947/mcxn947_romapi.h"

#define FLASH_SIZE  0x4000u
#define BASE_S      0x10000000u

#define ROMAPI_STUB_BASE           0x1303f000u
#define ROMAPI_STUB_IAP_MEM_WRITE  (ROMAPI_STUB_BASE + 0x270u)
#define ROMAPI_STUB_IAP_MEM_FILL   (ROMAPI_STUB_BASE + 0x280u)
#define ROMAPI_STUB_IAP_MEM_ERASE  (ROMAPI_STUB_BASE + 0x2a0u)
#define ROMAPI_STUB_IAP_MEM_ERASE_ALL (ROMAPI_STUB_BASE + 0x2c0u)
#define RETURN_ADDR 0x10000201u

static mm_u8 flash[FLASH_SIZE];

static int file_byte(const char *path, long off, mm_u8 *out)
{
    FILE *f = fopen(path, "rb");
    int c;
    if (f == 0) return -1;
    if (fseek(f, off, SEEK_SET) != 0) {
        fclose(f);
        return -1;
    }
    c = fgetc(f);
    fclose(f);
    if (c == EOF) return -1;
    *out = (mm_u8)c;
    return 0;
}

static mm_bool call(struct mm_cpu *cpu, struct mm_memmap *map, mm_u32 stub,
                    mm_u32 r1, mm_u32 r2, mm_u32 r3)
{
    cpu->r[0] = 0u;
    cpu->r[1] = r1;
    cpu->r[2] = r2;
    cpu->r[3] = r3;
    cpu->r[14] = RETURN_ADDR;
    cpu->r[15] = stub;
    if (!mm_mcxn947_romapi_handle(cpu, map)) return MM_FALSE;
    return cpu->r[0] == 0u;
}

static int expect(const char *path, long off, mm_u8 want, const char *what)
{
    mm_u8 b;
    if (file_byte(path, off, &b) != 0 || b != want) {
        fprintf(stderr, "%s: file byte 0x%lx is not 0x%02x\n", what, off, want);
        return 1;
    }
    return 0;
}

int main(void)
{
    struct mm_memmap map;
    struct mmio_region regions[128];
    struct mm_target_cfg cfg;
    struct mm_flash_persist persist;
    struct mm_cpu cpu;
    char path[] = "/tmp/m33mu_mcxn947_persist_XXXXXX";
    const char *paths[1];
    mm_u32 offsets[1];
    int fd;
    int fails = 0;

    memset(flash, 0xFF, sizeof(flash));
    memset(flash + 0x200u, 0x5A, 0x10u);    /* source for MEM_Write */
    fd = mkstemp(path);
    if (fd < 0) return 1;
    close(fd);
    paths[0] = path;
    offsets[0] = 0u;
    memset(&persist, 0, sizeof(persist));
    mm_flash_persist_build(&persist, flash, FLASH_SIZE, paths, offsets, 1);

    memset(&cfg, 0, sizeof(cfg));
    cfg.flash_base_s = BASE_S;
    cfg.flash_size_s = FLASH_SIZE;
    cfg.flash_base_ns = 0u;
    cfg.flash_size_ns = FLASH_SIZE;
    mm_memmap_init(&map, regions, 128u);
    (void)mm_memmap_configure_flash(&map, &cfg, flash, MM_TRUE);
    map.flash_base_s = BASE_S;
    map.flash_size_s = FLASH_SIZE;
    mm_mcxn947_mmio_reset();
    (void)mm_mcxn947_register_mmio(&map.mmio);
    mm_mcxn947_flash_bind(&map, flash, FLASH_SIZE, &persist, 0u);
    mm_mcxn947_romapi_set_tz_disabled(MM_TRUE);

    memset(&cpu, 0, sizeof(cpu));
    cpu.sec_state = MM_SECURE;

    if (!call(&cpu, &map, ROMAPI_STUB_IAP_MEM_WRITE, BASE_S + 0x1000u, 4u, BASE_S + 0x200u)) {
        fprintf(stderr, "MEM_Write failed\n");
        fails++;
    }
    fails += expect(path, 0x1000, 0x5A, "MEM_Write");

    if (!call(&cpu, &map, ROMAPI_STUB_IAP_MEM_ERASE, BASE_S + 0x1000u, 0x100u, 0u)) {
        fprintf(stderr, "MEM_Erase failed\n");
        fails++;
    }
    fails += expect(path, 0x1000, 0xFF, "MEM_Erase");

    if (!call(&cpu, &map, ROMAPI_STUB_IAP_MEM_FILL, BASE_S + 0x1100u, 4u, 0x33u)) {
        fprintf(stderr, "MEM_Fill failed\n");
        fails++;
    }
    fails += expect(path, 0x1100, 0x33, "MEM_Fill");

    if (!call(&cpu, &map, ROMAPI_STUB_IAP_MEM_ERASE_ALL, 0u, 0u, 0u)) {
        fprintf(stderr, "MEM_EraseAll failed\n");
        fails++;
    }
    fails += expect(path, 0x1100, 0xFF, "MEM_EraseAll");

    mm_flash_persist_build(&persist, 0, 0, 0, 0, 0);
    unlink(path);
    if (fails != 0) {
        printf("mcxn947_iap_persist_test: FAIL (%d)\n", fails);
        return 1;
    }
    printf("mcxn947_iap_persist_test: PASS\n");
    return 0;
}
