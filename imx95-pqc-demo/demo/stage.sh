#!/usr/bin/env bash
# Copy the demo onto the board. Uses ssh keys - no passwords here.
#
#   BOARD=torizon@<board-ip> WOLFBOOT=/path/to/wolfboot ./stage.sh
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
DEMO="$(cd "$HERE/.." && pwd)"
BOARD="${BOARD:?set BOARD, e.g. BOARD=torizon@192.168.1.50}"
WOLFBOOT="${WOLFBOOT:?set WOLFBOOT to your wolfBoot build directory}"
PAYLOAD="${PAYLOAD:-$DEMO/m7/build/zephyr/payload_v1_signed.bin}"

[ -f "$WOLFBOOT/wolfboot.elf" ] || { echo "no wolfboot.elf in $WOLFBOOT" >&2; exit 1; }
[ -f "$PAYLOAD" ] || { echo "no signed payload at $PAYLOAD" >&2; exit 1; }

ssh "$BOARD" 'mkdir -p ~/demo/fw ~/demo/results ~/bin'

scp -q "$DEMO/demo/twopane.py" "$DEMO/demo/demo-run.sh" \
       "$DEMO/demo/m7-start.sh" "$DEMO/demo/m7-console-tail.sh" \
       "$DEMO/demo/m7-rpmsg-log.sh" "$DEMO/demo/install-autostart.sh" \
       "$DEMO/demo/wolfssl-demo.service" "$BOARD:~/demo/"
scp -q "$DEMO/container/docker-compose.yml" "$BOARD:~/demo/"
scp -q "$WOLFBOOT/wolfboot.elf" "$BOARD:~/demo/fw/rproc-imx-rproc-fw"
scp -q "$PAYLOAD"               "$BOARD:~/demo/fw/payload.bin"

# memtool is how both the demo and the M7 console reader reach /dev/mem.
aarch64-linux-gnu-gcc -O2 -o /tmp/memtool "$DEMO/tools/memtool.c"
scp -q /tmp/memtool "$BOARD:~/bin/memtool"

# The demo is replayed by cutting power, so anything still in the page cache is
# lost on the next cycle and the board silently runs the previous payload.
# Flush, then verify the payload by content rather than by timestamp.
ssh "$BOARD" 'sync'
want=$(md5sum "$PAYLOAD" | cut -d' ' -f1)
got=$(ssh "$BOARD" 'md5sum ~/demo/fw/payload.bin' | cut -d' ' -f1)
if [ "$want" != "$got" ]; then
    echo "payload mismatch after staging: local $want, board $got" >&2
    exit 1
fi

echo "staged and verified. On the board:  sudo bash ~/demo/demo-run.sh /dev/tty1"
