#!/usr/bin/env bash
# Build and publish the Gallery image to Docker Hub. Run on an x86 host with
# docker buildx; the build cross-compiles for aarch64 rather than emulating it.
#
#   docker login
#   VERSION=1.0.0 ./publish.sh
#
# Pushes <REPO>:<VERSION> and <REPO>:latest. The Demo Gallery needs the package
# to be PUBLIC: Docker Hub repositories default to private, so set visibility to
# public once, in the repository's Settings, after the first push.
set -euo pipefail

REPO="${REPO:-wolfssl/wolfcrypt-pqc}"
VERSION="${VERSION:?set VERSION, e.g. VERSION=1.0.0}"
HERE="$(cd "$(dirname "$0")" && pwd)"
CTX="$HERE/../container"

# The Docker builder stage compiles wolfSSL itself, so all it needs in the
# context is a pristine source export - not the full cross-build that
# build-aarch64.sh also performs. Export it here when it is missing, so
# publishing is one command rather than two with a non-obvious ordering.
if [ ! -x "$CTX/wolfssl-src/configure" ]; then
    if [ -z "${WOLFSSL_REPO:-}" ]; then
        echo "wolfssl-src/ missing in $CTX" >&2
        echo "point WOLFSSL_REPO at a wolfSSL checkout and re-run, e.g." >&2
        echo "  WOLFSSL_REPO=~/GitHub/wolfssl VERSION=$VERSION $0" >&2
        echo "(or run ../container/build-aarch64.sh, which exports it as a side effect)" >&2
        exit 1
    fi
    echo "=== exporting $WOLFSSL_REPO @ ${WOLFSSL_REF:-HEAD} -> $CTX/wolfssl-src ==="
    # A pristine export, not a copy: the developer's checkout is configured
    # in-tree and autotools refuses to build against one.
    rm -rf "$CTX/wolfssl-src"
    mkdir -p "$CTX/wolfssl-src"
    git -C "$WOLFSSL_REPO" archive "${WOLFSSL_REF:-HEAD}" | tar -x -C "$CTX/wolfssl-src"
    ( cd "$CTX/wolfssl-src" && ./autogen.sh >autogen.log 2>&1 ) \
        || { tail -20 "$CTX/wolfssl-src/autogen.log"; exit 1; }
fi

ver=$(sed -n "s/^PACKAGE_VERSION='\(.*\)'/\1/p" "$CTX/wolfssl-src/configure" | head -1)
echo "=== building $REPO:$VERSION from wolfSSL ${ver:-unknown} ==="

# linux/arm64 only: every Torizon module this targets is Arm64, and a second
# architecture would double build time for an image nothing would pull.
docker buildx build --platform linux/arm64 \
    -t "$REPO:$VERSION" -t "$REPO:latest" \
    --push "$CTX"

echo
echo "pushed $REPO:$VERSION and $REPO:latest"
echo "if this was the first push, set the repository to PUBLIC at:"
echo "  https://hub.docker.com/r/$REPO/settings"
