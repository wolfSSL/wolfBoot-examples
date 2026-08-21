#!/usr/bin/env python3
"""Two-pane i.MX95 demo renderer.

Torizon OS ships no tmux, screen or dtach, and its rootfs is read-only OSTree,
so there is nothing to install. For a demo that is exactly two fixed panes of
append-only text, a full redraw is simpler and more predictable than shipping a
static multiplexer: no incremental cursor management, and a resize or a stray
escape sequence cannot corrupt the layout permanently.

  left  - wolfCrypt PQC benchmarks, in a container on the Cortex-A55 cluster
  right - wolfBoot ML-DSA-87 verified boot of the Cortex-M7

Run as root (the right pane reads /dev/mem via memtool), on the console you
want it displayed on:

    sudo python3 twopane.py            # current terminal
    sudo python3 twopane.py > /dev/tty1   # the HDMI console
"""

import os
import re
import shutil
import subprocess
import sys
import time

CONTAINER = os.environ.get("CONTAINER", "wolfcrypt-pqc")
MEMTOOL = os.environ.get("MEMTOOL", "/home/torizon/bin/memtool")
CONSOLE_ADDR = os.environ.get("CONSOLE_ADDR", "0x80F00000")
INTERVAL = float(os.environ.get("INTERVAL", "1.0"))

ANSI = re.compile(r"\x1b\[[0-9;]*[A-Za-z]")

# The benchmark's cycle columns derive from the 24 MHz generic timer, not the
# 1.8 GHz core clock, so they are wrong by roughly 75x. They must never appear
# on a demo screen someone might photograph. Strip them; ops/sec and ms are the
# numbers that are actually correct.
CYCLES = re.compile(r",?\s*\d+\s+cycles\s+[\d.]+\s+Cycles/op\s*$")
CPB = re.compile(r"\s*Cycles per byte\s*=\s*[\d.]+\s*$")

# The raw benchmark line is far too wide for half a console, and truncating it
# cuts off ops/sec - the one number worth showing. Condense to
# "<algorithm> <operation>   <rate>" so each result fits on one readable row.
OPS = re.compile(r"^(.*?)\s+\d+ ops took [\d.]+ sec, avg [\d.]+ ms,\s+([\d.]+) ops/sec")
THRU = re.compile(r"^(\S.*?)\s+[\d.]+ [KMG]iB took [\d.]+ seconds,\s+([\d.]+) ([KMG]iB/s)")


# The HDMI console on this board is 80x25, so each pane gets ~38 columns.
# Benchmark labels have to lose their redundant parameter fields to fit.
SHORTEN = (
    ("[      SECP256R1]", "P-256"),
    ("[ SECP256R1]", "P-256"),
    ("ML-KEM 512    128", "ML-KEM-512"),
    ("ML-KEM 768    192", "ML-KEM-768"),
    ("ML-KEM 1024   256", "ML-KEM-1024"),
    ("ML-DSA    44", "ML-DSA-44"),
    ("ML-DSA    65", "ML-DSA-65"),
    ("ML-DSA    87", "ML-DSA-87"),
    ("RSA     2048", "RSA-2048"),
)


def shorten(name):
    for a, b in SHORTEN:
        name = name.replace(a, b)
    return " ".join(name.split())


def condense(ln, width):
    m = THRU.match(ln)
    if m:
        unit = m.group(3) if width >= 46 else m.group(3).replace("iB/s", "B/s")
        value = f"{float(m.group(2)):,.1f} {unit}"
    else:
        m = OPS.match(ln)
        if not m:
            return ln
        unit = "ops/sec" if width >= 46 else "/s"
        value = f"{float(m.group(2)):,.0f} {unit}"

    # Pad the label to exactly what is left, so the value always lands flush
    # right and nothing is clipped.
    wname = max(6, width - len(value) - 1)
    return f"{shorten(m.group(1))[:wname]:<{wname}} {value:>{len(value)}}"

LEFT_TITLE = " A55 x6: wolfCrypt PQC "
RIGHT_TITLE = " M7: wolfBoot ML-DSA-87 "


def run(cmd):
    try:
        p = subprocess.run(cmd, shell=True, capture_output=True,
                           text=True, timeout=5)
        return p.stdout
    except Exception:
        return ""


def clean(text, width, drop_cycles=False):
    """Strip ANSI so column widths are computed on what is actually shown."""
    out = []
    for ln in text.splitlines():
        ln = ANSI.sub("", ln).expandtabs(4).rstrip()
        if drop_cycles:
            ln = CYCLES.sub("", ln)
            ln = CPB.sub("", ln)
            ln = condense(ln.rstrip().rstrip(","), width)
        out.append(ln)
    return out


def fit(lines, width, height, wrap=False):
    """Last `height` lines, fitted to `width`.

    The benchmark pane truncates: its lines are already condensed to fit, and a
    wrapped one would cost extra rows and break alignment. The wolfBoot pane
    wraps: its output is short, it is the point of the demo, and losing the end
    of "wc_MlDsaKey_Verify returned OK" would defeat the purpose.
    """
    out = []
    for ln in lines:
        if not wrap:
            out.append(ln[:width])
            continue
        if not ln:
            out.append("")
        while ln:
            out.append(ln[:width])
            ln = ln[width:]
    return out[-height:] if len(out) > height else out + [""] * (height - len(out))


def main():
    while True:
        cols, rows = shutil.get_terminal_size((100, 30))
        half = (cols - 3) // 2
        body = rows - 4

        left = fit(clean(run(f"docker logs --tail 400 {CONTAINER} 2>&1"), half, drop_cycles=True), half, body)
        right_raw = run(f"{MEMTOOL} con {CONSOLE_ADDR} 2>/dev/null")
        if not right_raw.strip():
            right_raw = "waiting for the Cortex-M7 to boot ...\n"
        right = fit(clean(right_raw, half), half, body, wrap=True)

        buf = ["\x1b[?25l\x1b[H"]   # hide cursor, home - no clear, see note above
        buf.append("\x1b[1;36m" + "NXP i.MX95  -  post-quantum on both clusters".center(cols) + "\x1b[0m")
        buf.append("\x1b[1;33m" + LEFT_TITLE.ljust(half) + " | " +
                   RIGHT_TITLE.ljust(half) + "\x1b[0m")
        buf.append("-" * cols)
        for i in range(body):
            buf.append(left[i].ljust(half) + " \x1b[1;30m|\x1b[0m " + right[i].ljust(half))

        # Pad to the full width so the previous frame is fully overwritten.
        # Pad on VISIBLE length: these lines contain colour escapes, and
        # ljust() on the raw string counts those bytes and silently clips real
        # characters off the right-hand pane.
        painted = []
        for i, ln in enumerate(buf):
            if i == 0:
                painted.append(ln)
                continue
            visible = len(ANSI.sub("", ln))
            painted.append(ln + " " * max(0, cols - visible))
        sys.stdout.write("\n".join(painted) + "\x1b[J")
        sys.stdout.flush()
        time.sleep(INTERVAL)


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        pass
    finally:
        # Always give the cursor back, or the console is left unusable.
        sys.stdout.write("\x1b[?25h\n")
        sys.stdout.flush()
