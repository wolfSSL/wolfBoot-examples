#!/usr/bin/env bash
# Right demo pane: live view of the Cortex-M7 console.
#
# Runs ON THE BOARD (the HDMI output is the board's own), so no host connection
# is involved once the demo is running.
#
# wolfBoot writes its log to a shared-memory ring at 0x80F00000 before any RPMsg
# endpoint exists, so this reads the ring directly rather than going through
# rpmsg_tty. That is deliberate: the ring is the only source that contains
# wolfBoot's own PQC verification output, and it needs no driver, no endpoint
# binding and no module load.
#
# memtool dumps the whole ring each call, so track how much has already been
# shown and print only what is new.
#
# Must run as root (/dev/mem). The demo launcher starts the whole tmux session
# under sudo so no password prompt can appear mid-demo.
set -uo pipefail

MEMTOOL=${MEMTOOL:-/home/torizon/bin/memtool}
ADDR=${ADDR:-0x80F00000}
INTERVAL=${INTERVAL:-0.5}

printf '\033[1;36m'
cat <<'BANNER'
+--------------------------------------------------------------+
|  NXP i.MX95  Cortex-M7   -   wolfBoot secure boot             |
|  ML-DSA-87 (NIST level 5) post-quantum verified boot          |
+--------------------------------------------------------------+
BANNER
printf '\033[0m\n'
echo "waiting for the M7 to boot ..."
echo

shown=0
while true; do
    out=$("$MEMTOOL" con "$ADDR" 2>/dev/null) || { sleep "$INTERVAL"; continue; }
    len=${#out}
    if [ "$len" -gt "$shown" ]; then
        printf '%s' "${out:$shown}"
        shown=$len
    elif [ "$len" -lt "$shown" ]; then
        # Ring was re-initialised (the M7 restarted): start over.
        printf '\n\033[1;33m--- M7 restarted ---\033[0m\n'
        printf '%s' "$out"
        shown=$len
    fi
    sleep "$INTERVAL"
done
