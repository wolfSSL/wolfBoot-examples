#!/usr/bin/env bash
# Read wolfBoot's verify log from the M7 over RPMsg. Runs ON THE BOARD as root.
#
# Order matters, and not in the obvious way. The payload relays the whole
# console ring as soon as its endpoint has a destination address, which the
# host supplies while binding the channel. That is strictly before
# /dev/ttyRPMSG* exists, so a reader can never be attached in time for the
# first pass, and the log it sends is discarded by a tty nobody has open.
#
# The payload therefore treats any byte written to the tty as a request to
# rewind and send the log again. So: attach the reader, then poke.
#
# The demo's right pane does NOT use this path - it reads the shared-memory
# ring directly, which needs no driver, no bind and no module load. This script
# is for showing the same log arriving as a normal Linux tty.
set -uo pipefail

SECS=${SECS:-8}
modprobe imx_rpmsg_tty 2>/dev/null || true

TTY=""
for _ in $(seq 1 30); do
    TTY=$(ls /dev/ttyRPMSG* 2>/dev/null | head -1)
    [ -n "$TTY" ] && break
    sleep 1
done
if [ -z "$TTY" ]; then
    echo "no /dev/ttyRPMSG* - is the M7 running? (cat /sys/class/remoteproc/remoteproc1/state)" >&2
    exit 1
fi

# raw so the log is not line-edited on its way through the line discipline
stty -F "$TTY" raw -echo clocal

timeout "$SECS" cat "$TTY" &
reader=$!
sleep 1
printf '\n' > "$TTY"     # request the replay
wait $reader 2>/dev/null
exit 0
