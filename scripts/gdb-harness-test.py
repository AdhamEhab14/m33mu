#!/usr/bin/env python3
"""Drive m33mu through its GDB monitor commands as a power-cut harness would.

Runs tests/firmware/test-gdb-harness (STM32H563): talks to it over a
captured UART, traces its flash operations, then power-cycles it from the
original image with a cut armed at its first program, and checks that the
cut operation never reached flash.

Usage: gdb-harness-test.py <path-to-m33mu>
Exit 0 on pass (or skip: no firmware and no cross compiler), 1 on failure.
"""

import os
import shutil
import socket
import subprocess
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
FWDIR = os.path.join(HERE, "..", "tests", "firmware", "test-gdb-harness")
APP = os.path.join(FWDIR, "app.bin")
UART = 0x40004800
SECTOR_ADDR = 0x0C000000 + 100 * 0x2000


class Gdb:
    """The few parts of the GDB remote protocol a harness needs."""

    def __init__(self, port):
        end = time.time() + 20
        while True:
            try:
                self.sock = socket.create_connection(("127.0.0.1", port), timeout=30)
                self.sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
                break
            except OSError:
                if time.time() > end:
                    raise
                time.sleep(0.1)
        self.buf = b""

    def _read_packet(self):
        while True:
            start = self.buf.find(b"$")
            if start >= 0:
                end = self.buf.find(b"#", start)
                if end >= 0 and len(self.buf) >= end + 3:
                    payload = self.buf[start + 1:end]
                    self.buf = self.buf[end + 3:]
                    self.sock.sendall(b"+")
                    return payload.decode()
            chunk = self.sock.recv(4096)
            if not chunk:
                raise RuntimeError("m33mu closed the connection")
            self.buf += chunk

    def send(self, payload):
        data = payload.encode()
        self.sock.sendall(b"$" + data + b"#%02x" % (sum(data) & 0xFF))

    def monitor(self, text):
        """Run a monitor command: (ok, console output)."""
        self.send("qRcmd," + text.encode().hex())
        out = ""
        while True:
            reply = self._read_packet()
            if reply.startswith("O") and reply != "OK":
                out += bytes.fromhex(reply[1:]).decode()
                continue
            if reply[:1] in ("S", "T"):
                # a stop reply is never the answer to qRcmd: the one m33mu
                # sends unasked when a client connects
                continue
            return reply == "OK", out

    def must(self, text):
        ok, out = self.monitor(text)
        if not ok:
            raise RuntimeError("monitor %s failed: %s" % (text, out.strip()))
        return out

    def cont(self):
        """Continue until the target stops; return the stop reply."""
        self.send("c")
        while True:
            reply = self._read_packet()
            if reply.startswith("O") and reply != "OK":
                continue
            return reply

    def run_for(self, us):
        self.must("run-for %d" % us)
        return self.cont()

    def uart_read(self):
        data = b""
        while True:
            fields = self.must("uart read 0x%x" % UART).split()
            if fields[2] != "-":
                data += bytes.fromhex(fields[2])
            if fields[1] == "0":
                return data


def check(cond, what):
    if not cond:
        raise AssertionError(what)


def run(m33mu, work):
    port = 3333 + os.getpid() % 2000
    log = open(os.path.join(work, "m33mu.log"), "w")
    proc = subprocess.Popen([m33mu, "--cpu", "stm32h563", "--gdb", "--port", str(port),
                             "--no-realtime", APP], stdout=log, stderr=subprocess.STDOUT)
    try:
        g = Gdb(port)
        g.must("uart attach 0x%x" % UART)
        check(g.must("flash ops").split() == ["flash", "ops", "0"], "ops at start")
        base, size = (int(v, 16) for v in g.must("flash info").split()[1:3])
        check((base, size) == (0x0C000000, 0x200000), "flash info")
        original = os.path.join(work, "original.bin")
        g.must("flash save " + original)

        # boot, talk over the UART, let it update flash
        trace = os.path.join(work, "trace.txt")
        g.must("flash trace " + trace)
        check(g.run_for(50000) == "S05", "stop at run-for deadline")
        check(g.uart_read() == b"BOOT\n", "boot banner")
        t0 = int(g.must("vtime").split()[1])
        g.must("uart write 0x%x 41" % UART)
        check(g.run_for(100000) == "S05", "second deadline")
        t1 = int(g.must("vtime").split()[1])
        check(t1 - t0 >= 64000000 // 10, "run-for advanced 100 ms of virtual time")
        check(g.uart_read() == b"GOT A\nDONE\n", "echo and done")
        ops = int(g.must("flash ops").split()[2])
        g.must("flash trace off")
        lines = open(trace).read().splitlines()
        check(len(lines) == ops and ops >= 2, "trace has every operation")
        check(lines[0] == "E 0x%08X #2000" % SECTOR_ADDR, "first op is the sector erase")
        check(lines[1].startswith("P 0x%08X " % SECTOR_ADDR), "then a program")
        updated = os.path.join(work, "updated.bin")
        g.must("flash save " + updated)
        img = open(updated, "rb").read()
        off = SECTOR_ADDR - base
        check(img[off:off + 4] == bytes.fromhex("11111111"), "programmed in flash")

        # back to the original image, power-cycle, cut at the first program
        g.must("flash load " + original)
        g.must("flash cut 1")
        g.must("power-cycle")
        check(g.must("flash cut status").strip() == "flash cut none", "nothing cut yet")
        g.run_for(50000)
        check(g.uart_read() == b"BOOT\n", "boots again after power-cycle")
        g.must("uart write 0x%x 42" % UART)
        check(g.run_for(100000) == "S05", "stops at the cut")
        status = g.must("flash cut status").split()
        check(status[:5] == ["flash", "cut", "1", "program", "0x%08x" % SECTOR_ADDR],
              "cut status names the program: %s" % status)
        check(g.uart_read() == b"GOT B\n", "nothing printed after the cut")
        check(g.run_for(10000) == "S05" and g.must("flash cut status").split()[2] == "1",
              "the core stays halted while the power is off")
        cut = os.path.join(work, "cut.bin")
        g.must("flash save " + cut)
        img = open(cut, "rb").read()
        check(img[off:off + 0x2000] == b"\xff" * 0x2000, "the erase happened, the program did not")

        ok, out = g.monitor("flash load /nonexistent/image.bin")
        check(not ok and out.startswith("error:"), "errors are reported")
        g.monitor("quit")
    finally:
        try:
            proc.wait(timeout=10)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.wait()
        log.close()


def main():
    m33mu = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, "..", "build", "m33mu")
    if not os.access(m33mu, os.X_OK):
        print("gdb-harness: emulator not found at %s" % m33mu, file=sys.stderr)
        return 1
    if not os.path.exists(APP) and shutil.which("arm-none-eabi-gcc"):
        subprocess.call(["make", "-C", FWDIR, "app.bin"],
                        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    if not os.path.exists(APP):
        print("gdb-harness: SKIP (no %s, no arm-none-eabi-gcc)" % APP, file=sys.stderr)
        return 0
    work = tempfile.mkdtemp(prefix="m33mu-gdb-harness-")
    try:
        run(m33mu, work)
    except (AssertionError, RuntimeError, OSError) as e:
        print("gdb-harness: FAIL -- %s" % e, file=sys.stderr)
        log = os.path.join(work, "m33mu.log")
        if os.path.exists(log):
            sys.stderr.write(open(log).read()[-3000:])
        return 1
    finally:
        shutil.rmtree(work, ignore_errors=True)
    print("gdb-harness: PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
