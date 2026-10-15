#!/usr/bin/env bash
# Write a boot-partition image to the target's eMMC over ssh, and verify it.
#
# The i.MX95 boot ROM reads its AHAB container set from whichever eMMC boot
# partition EXT_CSD selects. Writing the one that is NOT currently selected is
# safe: the board still boots the other one until you switch deliberately with
# emmc-select-boot.sh.
#
# Usage:
#   BOARD=user@host emmc-write-boot.sh <image> [0|1]      # default 1 = boot1
#
# sudo on the target must be usable; this deliberately embeds no password.
# ssh -t gives sudo a terminal so it can prompt for one. It also makes the
# remote echo CRLF, hence the tr on captured output.
set -euo pipefail

BOARD="${BOARD:?set BOARD=user@host}"
IMG="${1:?usage: BOARD=user@host $0 <image> [0|1]}"
PART="${2:-1}"
case "$PART" in 0|1) ;; *) echo "partition must be 0 or 1" >&2; exit 1 ;; esac
DEV="/dev/mmcblk0boot$PART"

[ -f "$IMG" ] || { echo "missing: $IMG" >&2; exit 1; }
LOCAL_MD5=$(md5sum "$IMG" | cut -d' ' -f1)
echo "image  : $IMG ($(stat -c %s "$IMG") bytes, md5 $LOCAL_MD5)"
echo "target : $BOARD $DEV"

echo "== which partition does the ROM boot from now? =="
ssh -t "$BOARD" "sudo mmc extcsd read /dev/mmcblk0 2>/dev/null | grep -i 'PARTITION_CONFIG'" 2>/dev/null | tr -d '\r' || true

echo "== staging =="
scp -q "$IMG" "$BOARD:/tmp/boot-image.bin"
ssh "$BOARD" 'sync'

echo "== writing =="
ssh -t "$BOARD" "sudo sh -c '
    set -e
    echo 0 > /sys/block/mmcblk0boot$PART/force_ro
    dd if=/tmp/boot-image.bin of=$DEV bs=1M conv=fsync status=none
    sync
    echo 1 > /sys/block/mmcblk0boot$PART/force_ro
'"

echo "== verifying readback =="
# Read back exactly as many bytes as were written. The boot partition is larger
# than the image (31.5 MiB on this module), so hashing the whole partition
# compares the image against the image plus whatever follows it, and reports a
# mismatch on a write that was perfectly good.
IMG_SZ=$(stat -c %s "$IMG")
REMOTE_MD5=$(ssh -t "$BOARD" "sudo dd if=$DEV bs=1M iflag=count_bytes count=$IMG_SZ 2>/dev/null | md5sum | cut -d' ' -f1" 2>/dev/null | tr -d '\r\n')
ssh "$BOARD" 'rm -f /tmp/boot-image.bin'
if [ "$LOCAL_MD5" != "$REMOTE_MD5" ]; then
    echo "MD5 MISMATCH: local $LOCAL_MD5, board $REMOTE_MD5" >&2
    exit 1
fi
echo "OK - readback matches ($REMOTE_MD5)"
echo "Nothing about the boot path changed yet; use emmc-select-boot.sh for that."
