#!/usr/bin/env bash
# Two-pane i.MX95 demo. Runs ON THE BOARD as root.
#
#   left  - wolfCrypt PQC benchmarks in a container on the Cortex-A55 cluster
#   right - wolfBoot ML-DSA-87 verified boot of the Cortex-M7
#
# Everything is local to the board, so once this is running nothing depends on
# a host being connected.
#
#   sudo bash demo-run.sh              # render on this terminal
#   sudo bash demo-run.sh /dev/tty1    # render on the HDMI console
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
    # A getty owns tty1; stop it first or the two fight over the console.
    systemctl stop getty@"$(basename "$TARGET")" 2>/dev/null || true
    exec python3 "$DEMO/twopane.py" > "$TARGET" 2>/dev/null
else
    exec python3 "$DEMO/twopane.py"
fi
