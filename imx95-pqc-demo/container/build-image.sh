#!/usr/bin/env bash
# Build the arm64 demo container.
#
# Cross-compiles in the builder stage on the native build platform, so this is
# fast even though the output image is linux/arm64. Validate the result under
# binfmt on the x86 bench before it ever touches the board.
#
# Publishing is deliberately opt-in: pass --push (and set REGISTRY) only when
# you actually intend to make the image public.
#
# Usage:
#   ./build-image.sh                 # build + load locally
#   ./build-image.sh --push          # build + push to $REGISTRY (requires login)

set -euo pipefail

DEMO_DIR="$(cd "${BASH_SOURCE[0]%/*}" && pwd)"
REGISTRY="${REGISTRY:-ghcr.io/wolfssl}"
IMAGE="${IMAGE:-wolfcrypt-pqc-imx95}"
TAG="${TAG:-latest}"
PLATFORM="${PLATFORM:-linux/arm64}"
REF="${REGISTRY}/${IMAGE}:${TAG}"

if [ ! -d "${DEMO_DIR}/wolfssl-src" ]; then
    echo "wolfssl-src/ missing -- run ./build-aarch64.sh first to export it" >&2
    exit 1
fi

OUTPUT=(--load)
if [ "${1:-}" = "--push" ]; then
    OUTPUT=(--push)
    echo "=== PUBLISHING to ${REF} ==="
    echo "=== this makes the image publicly pullable; Ctrl-C within 5s to abort ==="
    sleep 5
fi

set -x
docker buildx build \
    --platform "${PLATFORM}" \
    -t "${REF}" \
    -f "${DEMO_DIR}/Dockerfile" \
    "${OUTPUT[@]}" \
    "${DEMO_DIR}"
