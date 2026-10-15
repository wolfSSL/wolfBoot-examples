#!/usr/bin/env bash
# Cross-build wolfSSL for the Toradex SMARC i.MX95 (6x Cortex-A55, aarch64).
#
# Builds two configurations out-of-tree so the wolfSSL source checkout stays clean:
#   baseline     -- --enable-armasm            (NEON; works on any A55)
#   sha3-crypto  -- --enable-armasm=sha3-crypto (ARMv8.2 SHA3 instructions, FEAT_SHA3)
#
# The pair exists because ML-DSA has NO ARM assembly in wolfSSL: on aarch64 its
# speed comes entirely from the SHA-3/SHAKE backend, so this is the only lever
# that moves ML-DSA numbers. FEAT_SHA3 is optional on Cortex-A55, so the
# sha3-crypto build may not be runnable on the actual silicon -- check
# "grep Features /proc/cpuinfo" for 'sha3' on the board before shipping it.
#
# Static libraries only, so the binaries run under qemu-aarch64 with no
# LD_LIBRARY_PATH juggling.
#
# Usage: ./build-aarch64.sh [baseline|sha3-crypto|all]

set -euo pipefail

# Point this at your wolfSSL checkout:
#   WOLFSSL_REPO=/path/to/wolfssl ./build-aarch64.sh all
WOLFSSL_REPO="${WOLFSSL_REPO:?set WOLFSSL_REPO to your wolfSSL checkout}"
WOLFSSL_REF="${WOLFSSL_REF:-HEAD}"
DEMO_DIR="$(cd "${BASH_SOURCE[0]%/*}" && pwd)"
# Pristine export of the repo. The developer's checkout is already configured
# in-tree, and autotools refuses an out-of-tree build against a configured
# source dir. Exporting rather than running "make distclean" over there keeps
# their working tree and build state untouched.
WOLFSSL_SRC="${DEMO_DIR}/wolfssl-src"
HOST_TRIPLE=aarch64-linux-gnu
JOBS="$(nproc)"

export_src() {
    if [ -x "${WOLFSSL_SRC}/configure" ]; then
        echo "=== source export already present: ${WOLFSSL_SRC} ==="
        return
    fi
    echo "=== exporting ${WOLFSSL_REPO} @ ${WOLFSSL_REF} -> ${WOLFSSL_SRC} ==="
    rm -rf "${WOLFSSL_SRC}"
    mkdir -p "${WOLFSSL_SRC}"
    git -C "${WOLFSSL_REPO}" archive "${WOLFSSL_REF}" | tar -x -C "${WOLFSSL_SRC}"
    echo "=== autogen.sh ==="
    (cd "${WOLFSSL_SRC}" && ./autogen.sh > autogen.log 2>&1) \
        || { tail -40 "${WOLFSSL_SRC}/autogen.log"; exit 1; }
}

# Shared across both configurations. --enable-mldsa is REQUIRED: it is off by
# default, and without it the benchmark silently emits no ML-DSA rows at all
# rather than failing.
COMMON_OPTS=(
    "--host=${HOST_TRIPLE}"
    --enable-mlkem
    --enable-mldsa
    --enable-sp
    --enable-sp-asm
    --enable-keygen
    --enable-sha3
    --enable-curve25519
    --enable-ed25519
    --disable-shared
    --enable-static
)

build_one() {
    local name="$1" armasm="$2"
    local builddir="${DEMO_DIR}/build/${name}"

    echo "=== [${name}] configure (${armasm}) ==="
    rm -rf "${builddir}"
    mkdir -p "${builddir}"
    (
        cd "${builddir}"
        "${WOLFSSL_SRC}/configure" \
            "${COMMON_OPTS[@]}" \
            "${armasm}" \
            > configure.log 2>&1 || { tail -40 configure.log; exit 1; }
    )

    echo "=== [${name}] make -j${JOBS} ==="
    make -C "${builddir}" -j"${JOBS}" > "${builddir}/build.log" 2>&1 \
        || { tail -60 "${builddir}/build.log"; exit 1; }

    echo "=== [${name}] OK ==="
    file "${builddir}/wolfcrypt/benchmark/benchmark" || true
}

target="${1:-all}"
export_src
case "${target}" in
    baseline)    build_one baseline    "--enable-armasm" ;;
    sha3-crypto) build_one sha3-crypto "--enable-armasm=sha3-crypto" ;;
    all)
        build_one baseline    "--enable-armasm"
        build_one sha3-crypto "--enable-armasm=sha3-crypto"
        ;;
    *) echo "usage: $0 [baseline|sha3-crypto|all]" >&2; exit 2 ;;
esac
