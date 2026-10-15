#!/usr/bin/env bash
# Build AHAB container 2 (BL31 + BL33 + OP-TEE) and splice it into a copy of an
# existing eMMC boot-partition image.
#
# Container 1 - the ELE firmware, the System Manager, the OEI that trains DDR
# and the first A55 image - is copied through from the base image untouched, so
# the component that makes serial-download recovery possible is never rebuilt.
#
# Usage:
#   ahab-pack-container2.sh <base image> <bl31> <bl33> <optee> <output image>
#
# Requires mkimage_imx8 from NXP's imx-mkimage:
#   git clone https://github.com/nxp-imx/imx-mkimage
#   cd imx-mkimage && make bin      # produces ./mkimage_imx8
# Point MKIMAGE at it, or leave it on PATH.
set -euo pipefail

MKIMAGE="${MKIMAGE:-mkimage_imx8}"
CTNR2_OFF=$((0xcf800))      # where the i.MX95 boot ROM's second container sits

[ $# -eq 5 ] || { sed -n '2,20p' "$0"; exit 1; }
BASE="$1"; BL31="$2"; BL33="$3"; OPTEE="$4"; OUT="$5"

command -v "$MKIMAGE" >/dev/null || { echo "mkimage_imx8 not found; set MKIMAGE" >&2; exit 1; }
for f in "$BASE" "$BL31" "$BL33" "$OPTEE"; do
    [ -f "$f" ] || { echo "missing: $f" >&2; exit 1; }
done

TMP=$(mktemp -d); trap 'rm -rf "$TMP"' EXIT

# mkimage appends its own commit stamp to the payload it signs over.
"$MKIMAGE" -commit > "$TMP/head.hash"
cat "$BL33" "$TMP/head.hash" > "$TMP/bl33-hash.bin"

"$MKIMAGE" -soc IMX9 -cntr_version 2 -c \
    -ap "$BL31"             a55 0x8A200000 \
    -ap "$TMP/bl33-hash.bin" a55 0x90200000 \
    -ap "$OPTEE"            a55 0x8C000000 \
    -out "$TMP/container2.img" > "$TMP/mkimage.log" 2>&1 \
  || { echo "mkimage failed:" >&2; tail -20 "$TMP/mkimage.log" >&2; exit 1; }

cp "$BASE" "$OUT"
dd if="$TMP/container2.img" of="$OUT" bs=1 seek=$CTNR2_OFF conv=notrunc status=none

[ "$(stat -c %s "$OUT")" = "$(stat -c %s "$BASE")" ] \
  || { echo "REFUSING: $OUT changed size against the base image" >&2; exit 1; }

echo "container 2: $(stat -c %s "$TMP/container2.img") bytes at $(printf 0x%x $CTNR2_OFF)"
echo "output     : $OUT"
