#!/usr/bin/env bash
# Build the Zephyr RPMsg payload for the i.MX95 Cortex-M7, linked into
# wolfBoot's boot partition.
#
# Requires a Zephyr workspace and an arm-none-eabi toolchain. The Zephyr SDK's
# arm-zephyr-eabi works too; this defaults to the system toolchain because it is
# usually already present.
#
#   ZEPHYR_BASE=~/zephyrproject/zephyr ./build.sh
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
BUILD="${BUILD:-$HERE/build}"
BOARD="${BOARD:-imx95_evk/mimx9596/m7/ddr}"

: "${ZEPHYR_BASE:?set ZEPHYR_BASE to your Zephyr checkout}"
export ZEPHYR_BASE
export ZEPHYR_TOOLCHAIN_VARIANT="${ZEPHYR_TOOLCHAIN_VARIANT:-gnuarmemb}"
export GNUARMEMB_TOOLCHAIN_PATH="${GNUARMEMB_TOOLCHAIN_PATH:-/usr}"

# The upstream openamp_rsc_table board overlay supplies the shared-memory and
# mailbox nodes; ours relocates the image and enables the MU and the console
# region. Both are needed.
SAMPLE_OVERLAY="$ZEPHYR_BASE/samples/subsys/ipc/openamp_rsc_table/boards/imx95_evk_mimx9596_m7.overlay"

west build -p always -b "$BOARD" -d "$BUILD" "$HERE/zephyr-app" \
    -- -DDTC_OVERLAY_FILE="$HERE/imx95_wolfboot.overlay;$SAMPLE_OVERLAY"

arm-none-eabi-objcopy -O binary \
    "$BUILD/zephyr/wolfboot_openamp.elf" "$BUILD/zephyr/payload.bin"

echo
echo "payload: $BUILD/zephyr/payload.bin"
echo "sign it with wolfBoot's keytool, e.g.:"
echo "  IMAGE_HEADER_SIZE=12288 ML_DSA_LEVEL=5 ./tools/keytools/sign --ml_dsa --sha256 \\"
echo "      $BUILD/zephyr/payload.bin wolfboot_signing_private_key.der 1"
