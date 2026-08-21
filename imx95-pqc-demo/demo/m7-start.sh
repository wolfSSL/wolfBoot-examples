#!/usr/bin/env bash
# Load wolfBoot + the signed payload onto the Cortex-M7 and release the core.
# Runs ON THE BOARD as root, so the demo needs no host connection.
#
# The M7 can only be started ONCE per Linux boot - "echo stop" fails on this BSP
# with "Interrupted system call" and the core stays running. So a second run of
# the demo needs a full power cycle, not a restart. That is why the demo's
# "reboot beat" is a real power cycle.
set -euo pipefail

FW=/home/torizon/demo/fw
M=/home/torizon/bin/memtool
RP=/sys/class/remoteproc/remoteproc1
BOOT_ADDR=0x80100000
STATUS=0x80F10000

if [ "$(cat $RP/state)" = "running" ]; then
    echo "M7 is already running - power cycle the board to run the demo again" >&2
    exit 1
fi

$M fill $STATUS 32 0
$M load $BOOT_ADDR "$FW/payload.bin"
echo "$FW" > /sys/module/firmware_class/parameters/path
echo start > $RP/state
sleep 1
echo "M7 state: $(cat $RP/state)"
