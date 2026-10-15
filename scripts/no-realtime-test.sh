#!/bin/sh
# --no-realtime: virtual time no longer paced against the wall clock.
#
# tests/firmware/test-no-realtime sleeps in WFI for 200 SysTick periods
# (over 3.3 virtual seconds at any core clock), then hits BKPT 0x7f.
# With --no-realtime it must finish well inside a 2 s wall-clock timeout;
# without it, the same run must still be paced and hit that timeout.
#
# Usage: no-realtime-test.sh <path-to-m33mu>

M33MU="${1:-./build/m33mu}"
HERE="$(dirname "$0")/.."
FWDIR="$HERE/tests/firmware/test-no-realtime"
APP="$FWDIR/app.bin"

if [ ! -x "$M33MU" ]; then
    echo "no-realtime: emulator not found at $M33MU" >&2
    exit 1
fi
if [ ! -f "$APP" ]; then
    # The binary is gitignored, so build it when a cross compiler is around.
    if command -v arm-none-eabi-gcc >/dev/null 2>&1; then
        make -C "$FWDIR" app.bin >/dev/null 2>&1 || true
    fi
fi
if [ ! -f "$APP" ]; then
    echo "no-realtime: SKIP (no $APP, no arm-none-eabi-gcc)" >&2
    exit 0
fi

if ! "$M33MU" --cpu stm32h563 --no-realtime --expect-bkpt 0x7f --timeout 2 \
        "$APP" >/dev/null 2>&1; then
    echo "no-realtime: FAIL -- --no-realtime run did not finish within 2 s" >&2
    exit 1
fi

"$M33MU" --cpu stm32h563 --expect-bkpt 0x7f --timeout 2 "$APP" >/dev/null 2>&1
rc=$?
if [ "$rc" -ne 127 ]; then
    echo "no-realtime: FAIL -- paced run exited $rc, expected the 2 s timeout (127)" >&2
    exit 1
fi
echo "no-realtime: PASS"
