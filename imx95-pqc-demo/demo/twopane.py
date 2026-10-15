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
A55_CONSOLE_ADDR = os.environ.get("A55_CONSOLE_ADDR", "0x80F20000")
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
# The A55 boot log is long and most of it is bookkeeping: the handoff register
# dump, the MBR walk, and a line per FIT sub-image copied. Only the chain of
# trust belongs on a demo screen, and the pane holds far fewer rows than the log
# has lines, so without this the verification scrolls off before anyone reads it.
BOOT_KEEP = re.compile(
    r"^(wolfBoot:|scmi:|usdhc:|Boot partition:|Booting version|"
    r"Checking image integrity|Verifying image signature|info: ML-DSA|"
    r"info: using ML-DSA|info: wc_MlDsaKey_Verify|Firmware Valid|Booting at)")

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

LEFT_TITLE = " Cortex-A55: wolfCrypt PQC (on 1 of 6 cores) "
LEFT_BOOT_TITLE = " Cortex-A55: wolfBoot ML-DSA-87 "
RIGHT_TITLE = " Cortex-M7: wolfBoot ML-DSA-87 "


def run(cmd):
    try:
        p = subprocess.run(cmd, shell=True, capture_output=True,
                           text=True, timeout=5)
        return p.stdout
    except Exception:
        return ""


class LogTail:
    """Follow a container's logs in one persistent `docker logs -f` process,
    buffering the last N lines. Polling `docker logs` every tick spawned the
    CLI several times a second (86% CPU at a 0.2s tick) and beat against the
    benchmark's ~1s output, so updates aliased into ragged multi-second jumps.
    Streaming keeps the buffer current with one process, so the render loop
    reads memory (cheap) and always sees the latest line."""

    def __init__(self, container, maxlines=400):
        import collections
        import threading
        self.buf = collections.deque(maxlen=maxlines)
        self._container = container
        self._lock = threading.Lock()
        # Set whenever a new line arrives, so the render loop can wake on the
        # newline instead of polling on a timer. This is what keeps the display
        # in lockstep with the benchmark: one result line -> one redraw.
        self.event = threading.Event()
        t = threading.Thread(target=self._follow, daemon=True)
        t.start()

    def _follow(self):
        while True:
            try:
                p = subprocess.Popen(
                    ["docker", "logs", "-f", "--tail", "400",
                     self._container],
                    stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                    text=True, bufsize=1)
                for line in p.stdout:
                    with self._lock:
                        self.buf.append(line.rstrip("\n"))
                    self.event.set()
            except Exception:
                pass
            # Container gone/restarting: wait and reattach.
            time.sleep(2)

    def text(self):
        with self._lock:
            return "\n".join(self.buf)


def boot_lines(text):
    """Keep only the chain-of-trust lines from wolfBoot's own boot log."""
    return [ln for ln in text.splitlines() if BOOT_KEEP.match(ln.strip())]


def clean(text, width, drop_cycles=False):
    """Strip ANSI so column widths are computed on what is actually shown.

    With drop_cycles (the benchmark pane) this keeps ONLY result lines and
    drops the banner/header chatter - the wolfSSL version block, the CPU flags,
    the "Keccak: NEON only" / "no FEAT_SHA3" implementation notes, the dashed
    separators. The pane title already says what this is; the results are the
    point."""
    out = []
    for ln in text.splitlines():
        ln = ANSI.sub("", ln).expandtabs(4).rstrip()
        if drop_cycles:
            stripped = CPB.sub("", CYCLES.sub("", ln)).rstrip().rstrip(",")
            if not (OPS.match(stripped) or THRU.match(stripped)):
                continue  # not a benchmark result -> drop as noise
            out.append(condense(stripped, width))
        else:
            out.append(ln)
    return out


def fit(lines, width, height, wrap=False):
    """Last `height` lines, fitted to `width`.

    Both panes wrap. The wolfBoot panes must: their output is the point of the
    demo, and losing the end of "wc_MlDsaKey_Verify returned OK" would defeat
    it. The benchmark pane is already condensed to the pane width, so wrapping
    is normally a no-op there, and it is still what we want in the narrow case
    condense() cannot squeeze (a long value clamps the label at six columns and
    the line runs over) - an extra row beats a silently cut number.
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
    # Torizon's kernel has no fbdev emulation, so a VT can never reach the
    # HDMI. RENDER=drm draws straight into a DRM dumb buffer instead; the
    # default remains the terminal for ssh/serial use.
    fb = None
    if os.environ.get("RENDER", "tty") == "drm":
        from drmfb import DrmFramebuffer
        fb = DrmFramebuffer(scale=int(os.environ.get("DRM_SCALE", "2")))
    logtail = LogTail(CONTAINER)
    while True:
        if fb is not None:
            cols, rows = fb.cols, fb.rows
        else:
            cols, rows = shutil.get_terminal_size((100, 30))
        half = (cols - 3) // 2
        body = rows - 4

        # The left pane tells the A55's story in the order it happened: first
        # wolfBoot verifying the kernel it booted (read back from the DDR ring,
        # since that output is long gone from the console by the time anything
        # can render), then the benchmarks, from the moment the container has a
        # result worth showing.
        bench = clean(logtail.text(), half, drop_cycles=True)
        if bench:
            left_title = LEFT_TITLE
            left = fit(bench, half, body, wrap=True)
        else:
            left_title = LEFT_BOOT_TITLE
            boot_raw = run(f"{MEMTOOL} con {A55_CONSOLE_ADDR} 2>/dev/null")
            boot = boot_lines(ANSI.sub("", boot_raw))
            if not boot:
                boot = ["waiting for the benchmark container ..."]
            left = fit(boot, half, body, wrap=True)
        right_raw = run(f"{MEMTOOL} con {CONSOLE_ADDR} 2>/dev/null")
        if not right_raw.strip():
            right_raw = "waiting for the Cortex-M7 to boot ...\n"
        right = fit(clean(right_raw, half), half, body, wrap=True)

        buf = ["\x1b[?25l\x1b[H"]   # hide cursor, home - no clear, see note above
        buf.append("\x1b[1;36m" + "NXP i.MX95  -  post-quantum on both clusters".center(cols) + "\x1b[0m")
        buf.append("\x1b[1;33m" + left_title.ljust(half) + " | " +
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
        if fb is not None:
            # Drop the cursor-control prefix; drmfb parses only SGR colours.
            painted[0] = painted[0].replace("\x1b[?25l\x1b[H", "")
            fb.draw_lines(painted, ANSI)
        else:
            sys.stdout.write("\n".join(painted) + "\x1b[J")
            sys.stdout.flush()
        # Wake immediately on a new benchmark line (event-driven, so the redraw
        # tracks the newline exactly); otherwise fall through after INTERVAL to
        # refresh the M7 pane. No busy polling either way.
        logtail.event.wait(timeout=INTERVAL)
        logtail.event.clear()


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        pass
    finally:
        # Always give the cursor back, or the console is left unusable.
        sys.stdout.write("\x1b[?25h\n")
        sys.stdout.flush()
