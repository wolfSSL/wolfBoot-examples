#!/usr/bin/env bash
# Switch the board between the stock U-Boot chain and the wolfBoot chain.
# Runs ON THE BOARD, so it needs no host, no network and no ssh:
#
#   sudo bash ~/demo/switch-boot.sh          # flip to the other chain
#   sudo bash ~/demo/switch-boot.sh 0        # force stock U-Boot (boot0)
#   sudo bash ~/demo/switch-boot.sh 1        # force wolfBoot (boot1)
#   sudo bash ~/demo/switch-boot.sh --show   # report only, change nothing
#
# The i.MX95 boot ROM takes its AHAB container set from whichever eMMC boot
# partition EXT_CSD selects, so this is an EXT_CSD write plus a reboot. Both
# chains share the eMMC rootfs, so this script is present either way and the
# switch works in both directions.
set -uo pipefail

MMC=/dev/mmcblk0
# PARTITION_CONFIG bits [5:3]: 1 = boot0, 2 = boot1. mmc-utils uses the same
# numbering for "mmc bootpart enable".
name_of() { case "$1" in 1) echo "boot0 (stock U-Boot chain)" ;; 2) echo "boot1 (wolfBoot chain)" ;; *) echo "unknown ($1)" ;; esac; }

if [ "$(id -u)" != "0" ]; then
    echo "needs root: sudo bash $0 $*" >&2
    exit 1
fi

current() {
    local v
    v=$(mmc extcsd read $MMC 2>/dev/null \
        | grep -i "PARTITION_CONFIG" | grep -o "0x[0-9a-fA-F]*" | head -1)
    [ -n "$v" ] || return 1
    echo $(( (v >> 3) & 7 ))
}

CUR=$(current) || { echo "cannot read PARTITION_CONFIG" >&2; exit 1; }
echo "current: $(name_of "$CUR")"

case "${1:-}" in
    --show|-s) exit 0 ;;
    0) NEW=1 ;;
    1) NEW=2 ;;
    "") NEW=$(( CUR == 1 ? 2 : 1 )) ;;
    *)  echo "usage: $0 [0|1|--show]" >&2; exit 1 ;;
esac

if [ "$NEW" = "$CUR" ]; then
    echo "already on $(name_of "$CUR") - nothing to do"
    exit 0
fi

# Refuse to switch into a partition with no container. The ROM would find
# nothing to boot, and recovering from that needs the B4 button and uuu.
DEV=/dev/mmcblk0boot$(( NEW - 1 ))
TAG=$(dd if=$DEV bs=1 skip=32771 count=1 2>/dev/null | od -An -tx1 | tr -d ' \n')
if [ "$TAG" != "87" ]; then
    echo "$DEV has no AHAB container at 0x8000 (tag 0x$TAG) - refusing" >&2
    exit 1
fi

echo "switching to: $(name_of "$NEW")"
mmc bootpart enable "$NEW" 1 $MMC || { echo "mmc bootpart failed" >&2; exit 1; }
AFTER=$(current)
if [ "$AFTER" != "$NEW" ]; then
    echo "PARTITION_CONFIG did not take (still $(name_of "$AFTER"))" >&2
    exit 1
fi
sync
echo "now set to: $(name_of "$AFTER")"
echo
echo "Power-cycle the board to boot it. Run this again to switch back."
