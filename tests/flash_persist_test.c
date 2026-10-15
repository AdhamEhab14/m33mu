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

#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "m33mu/flash_persist.h"

#define FLASH_SIZE 0x2000u
#define APP_OFFSET 0x1000u

static int make_file(char *path, const mm_u8 *data, size_t len)
{
    int fd = mkstemp(path);
    if (fd < 0) return -1;
    if (len != 0u && write(fd, data, len) != (ssize_t)len) {
        close(fd);
        return -1;
    }
    close(fd);
    return 0;
}

static long file_size(const char *path)
{
    struct stat st;
    if (stat(path, &st) != 0) return -1;
    return (long)st.st_size;
}

static int read_at(const char *path, long off, mm_u8 *out, size_t len)
{
    FILE *f = fopen(path, "rb");
    size_t got;
    if (f == 0) return -1;
    if (fseek(f, off, SEEK_SET) != 0) {
        fclose(f);
        return -1;
    }
    got = fread(out, 1, len, f);
    fclose(f);
    return got == len ? 0 : -1;
}

/* Two BINs, the second loaded at an offset ("app.bin:0x1000"). Each file holds
 * only its own range: a write in the second range lands at the matching
 * position inside the second file, and neither file grows past its range. */
static int test_offset_image(void)
{
    static mm_u8 flash[FLASH_SIZE];
    struct mm_flash_persist persist;
    char boot[] = "/tmp/m33mu_persist_boot_XXXXXX";
    char app[] = "/tmp/m33mu_persist_app_XXXXXX";
    const char *paths[2];
    mm_u32 offsets[2];
    mm_u8 b[4];
    int rc = 1;

    memset(flash, 0xFF, sizeof(flash));
    memset(flash, 0x11, 0x100u);
    memset(flash + APP_OFFSET, 0x22, 0x100u);
    if (make_file(boot, flash, 0x100u) != 0) return 1;
    if (make_file(app, flash + APP_OFFSET, 0x100u) != 0) {
        unlink(boot);
        return 1;
    }

    paths[0] = boot;
    paths[1] = app;
    offsets[0] = 0u;
    offsets[1] = APP_OFFSET;
    memset(&persist, 0, sizeof(persist));
    mm_flash_persist_build(&persist, flash, FLASH_SIZE, paths, offsets, 2);

    flash[APP_OFFSET + 4u] = 0x5A;
    mm_flash_persist_flush(&persist, APP_OFFSET + 4u, 1u);
    flash[0x10u] = 0xA5;
    mm_flash_persist_flush(&persist, 0x10u, 1u);

    if (file_size(boot) != (long)APP_OFFSET) {
        fprintf(stderr, "boot file is %ld bytes, want 0x%x\n", file_size(boot), APP_OFFSET);
        goto out;
    }
    if (file_size(app) != (long)(FLASH_SIZE - APP_OFFSET)) {
        fprintf(stderr, "app file is %ld bytes, want 0x%x\n", file_size(app), FLASH_SIZE - APP_OFFSET);
        goto out;
    }
    if (read_at(app, 4, b, 1) != 0 || b[0] != 0x5A) {
        fprintf(stderr, "app write not at file offset 4\n");
        goto out;
    }
    if (read_at(app, 0, b, 4) != 0 || b[0] != 0x22 || b[3] != 0x22) {
        fprintf(stderr, "app file head clobbered\n");
        goto out;
    }
    if (read_at(boot, 0x10, b, 1) != 0 || b[0] != 0xA5) {
        fprintf(stderr, "boot write not at file offset 0x10\n");
        goto out;
    }
    rc = 0;
out:
    mm_flash_persist_build(&persist, 0, 0, 0, 0, 0);
    unlink(boot);
    unlink(app);
    return rc;
}

int main(void)
{
    if (test_offset_image() != 0) {
        printf("flash_persist_test: FAIL offset image\n");
        return 1;
    }
    printf("flash_persist_test: PASS\n");
    return 0;
}
