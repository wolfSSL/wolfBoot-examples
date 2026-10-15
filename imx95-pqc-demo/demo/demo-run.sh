#!/usr/bin/env bash
# Two-pane i.MX95 demo. Runs ON THE BOARD as root.
#
#   left  - wolfBoot's own ML-DSA-87 verified boot of Linux, replaced by the
#           wolfCrypt PQC benchmarks once the container has results
#   right - wolfBoot ML-DSA-87 verified boot of the Cortex-M7
#
# Everything is local to the board, so once this is running nothing depends on
# a host being connected.
#
#   sudo bash demo-run.sh              # render on this terminal
#   sudo bash demo-run.sh /dev/tty1    # render on the DisplayPort panel
#
# The M7 can only be started ONCE per Linux boot, so re-running the demo needs a
# full board power cycle. That is the "reboot beat" - and it has to come from
# the host relay or a person, since the board cannot power-cycle itself.
set -uo pipefail

DEMO=/home/torizon/demo
TARGET=${1:-}

echo "== starting the benchmark container =="
# "down" first, deliberately. A container created by an earlier run survives a
# reboot while its compose network does not, so a bare "up -d" fails with
# "network <id> not found" and the left pane silently stays empty. Since the
# demo's replay beat IS a power cycle, that is exactly when it would bite.
( cd "$DEMO" && docker compose down --remove-orphans >/dev/null 2>&1 || true )
( cd "$DEMO" && docker compose up -d ) || exit 1

echo "== releasing the Cortex-M7 =="
bash "$DEMO/m7-start.sh" || echo "  (M7 already running - power cycle to replay the boot)"

echo "== rendering =="
if [ -n "$TARGET" ]; then
    # Torizon's kernel has no fbdev emulation, so text written to a VT reaches
    # no display: the renderer has to own the DisplayPort through KMS. Take the
    # screen away from everything else that wants it first - the getty on the
    # console, the framebuffer console redrawing over the KMS output, and
    # Torizon's pairing overlay - then render as the sole DRM master.
    systemctl stop getty@"$(basename "$TARGET")" 2>/dev/null || true
    for v in /sys/class/vtconsole/vtcon*; do
        if grep -qi 'frame buffer' "$v/name" 2>/dev/null; then
            echo 0 > "$v/bind" 2>/dev/null || true
        fi
    done
    docker stop torizon-easy-pairing-bash-1 >/dev/null 2>&1 || true
    # stderr is left alone on purpose: the DRM path is the most failure-prone
    # part of the demo (hotplug, EDID, DRM-master contention), and discarding
    # it turns a black screen into a silent restart loop with nothing in the
    # journal to explain it.
    exec env RENDER=drm python3 "$DEMO/twopane.py"
else
    exec python3 "$DEMO/twopane.py"
fi
