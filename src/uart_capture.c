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

#include <stdlib.h>
#include <string.h>
#include "m33mu/uart_capture.h"
#include "m33mu/target_hal.h"

#define CAPTURE_SLOTS 4
#define RX_QUEUE 4096u

struct capture {
    mm_bool used;
    mm_u32 base;
    mm_u8 *tx;
    size_t tx_len;
    size_t tx_cap;
    mm_u64 dropped;
    mm_u8 rx[RX_QUEUE];
    size_t rx_head;
    size_t rx_len;
};

static struct capture captures[CAPTURE_SLOTS];

static struct capture *find(mm_u32 base)
{
    int i;
    for (i = 0; i < CAPTURE_SLOTS; ++i) {
        if (captures[i].used && captures[i].base == base) return &captures[i];
    }
    return 0;
}

static size_t cap_write_tx(void *opaque, const mm_u8 *data, size_t len)
{
    struct capture *c = (struct capture *)opaque;
    size_t room;
    size_t keep;
    if (c == 0 || data == 0) return len;
    room = MM_UART_CAPTURE_MAX - c->tx_len;
    keep = (len < room) ? len : room;
    if (c->tx_len + keep > c->tx_cap) {
        size_t want = (c->tx_cap == 0u) ? 4096u : c->tx_cap;
        mm_u8 *grown;
        while (want < c->tx_len + keep) want *= 2u;
        if (want > MM_UART_CAPTURE_MAX) want = MM_UART_CAPTURE_MAX;
        grown = (mm_u8 *)realloc(c->tx, want);
        if (grown == 0) {
            c->dropped += len;
            return len;
        }
        c->tx = grown;
        c->tx_cap = want;
    }
    memcpy(c->tx + c->tx_len, data, keep);
    c->tx_len += keep;
    c->dropped += len - keep;
    /* the guest's UART never stalls: it was sent */
    return len;
}

static mm_bool cap_read_rx(void *opaque, mm_u8 *byte_out)
{
    struct capture *c = (struct capture *)opaque;
    if (c == 0 || byte_out == 0 || c->rx_len == 0u) return MM_FALSE;
    *byte_out = c->rx[c->rx_head];
    c->rx_head = (c->rx_head + 1u) % RX_QUEUE;
    c->rx_len--;
    return MM_TRUE;
}

static void cap_close(void *opaque)
{
    /* The guest disabled the UART; the capture stays for the harness. */
    (void)opaque;
}

static const struct mm_uart_backend_ops capture_ops = {
    cap_write_tx,
    cap_read_rx,
    cap_close
};

mm_bool mm_uart_capture_attach(mm_u32 base)
{
    int i;
    struct capture *c = find(base);
    if (c != 0) return MM_TRUE;
    for (i = 0; i < CAPTURE_SLOTS; ++i) {
        if (!captures[i].used) {
            c = &captures[i];
            break;
        }
    }
    if (c == 0) return MM_FALSE;
    memset(c, 0, sizeof(*c));
    c->used = MM_TRUE;
    c->base = base;
    if (!mm_uart_backend_attach(base, "capture", &capture_ops, c)) {
        c->used = MM_FALSE;
        return MM_FALSE;
    }
    return MM_TRUE;
}

void mm_uart_capture_detach_all(void)
{
    int i;
    for (i = 0; i < CAPTURE_SLOTS; ++i) {
        if (captures[i].used) {
            mm_uart_backend_detach(captures[i].base);
            free(captures[i].tx);
            memset(&captures[i], 0, sizeof(captures[i]));
        }
    }
}

size_t mm_uart_capture_take(mm_u32 base, mm_u8 *out, size_t cap)
{
    struct capture *c = find(base);
    size_t n;
    if (c == 0 || out == 0) return 0u;
    n = (c->tx_len < cap) ? c->tx_len : cap;
    memcpy(out, c->tx, n);
    memmove(c->tx, c->tx + n, c->tx_len - n);
    c->tx_len -= n;
    return n;
}

size_t mm_uart_capture_pending(mm_u32 base)
{
    struct capture *c = find(base);
    return (c != 0) ? c->tx_len : 0u;
}

mm_u64 mm_uart_capture_dropped(mm_u32 base)
{
    struct capture *c = find(base);
    return (c != 0) ? c->dropped : 0u;
}

mm_bool mm_uart_capture_inject(mm_u32 base, const mm_u8 *data, size_t len)
{
    struct capture *c = find(base);
    size_t i;
    if (c == 0 || data == 0) return MM_FALSE;
    if (len > RX_QUEUE - c->rx_len) return MM_FALSE;
    for (i = 0; i < len; ++i) {
        c->rx[(c->rx_head + c->rx_len) % RX_QUEUE] = data[i];
        c->rx_len++;
    }
    return MM_TRUE;
}
