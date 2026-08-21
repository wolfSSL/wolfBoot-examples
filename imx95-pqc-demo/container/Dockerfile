# wolfCrypt PQC benchmark container for Toradex SMARC i.MX95 / Torizon OS.
#
# The builder stage runs on the NATIVE build platform and cross-compiles for
# aarch64, rather than compiling arm64 under qemu emulation. Same artifacts,
# minutes instead of the better part of an hour.
#
# Both wolfSSL configurations are shipped in the image so the SHA-3 A/B can be
# run on the board without a rebuild, and so the entrypoint can pick the right
# one at runtime from /proc/cpuinfo.

# ---------------------------------------------------------------- builder ---
FROM --platform=$BUILDPLATFORM debian:bookworm AS builder

RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
        autoconf automake libtool make gcc \
        gcc-aarch64-linux-gnu binutils-aarch64-linux-gnu \
        libc6-dev-arm64-cross \
        ca-certificates \
    && rm -rf /var/lib/apt/lists/*

COPY wolfssl-src /src/wolfssl
WORKDIR /src/wolfssl

# --enable-mldsa is REQUIRED: it is off by default, and without it the
# benchmark silently emits no ML-DSA rows rather than failing.
ENV WOLF_COMMON="--host=aarch64-linux-gnu --enable-mlkem --enable-mldsa \
--enable-sp --enable-sp-asm --enable-keygen --enable-sha3 \
--enable-curve25519 --enable-ed25519 --disable-shared --enable-static"

# baseline: NEON only, runs on any Cortex-A55.
RUN mkdir -p /build/baseline && cd /build/baseline \
    && /src/wolfssl/configure $WOLF_COMMON --enable-armasm \
    && make -j"$(nproc)"

# sha3-crypto: ARMv8.2 SHA3 instructions. Requires FEAT_SHA3 on the silicon;
# the entrypoint checks /proc/cpuinfo before selecting this one.
RUN mkdir -p /build/sha3-crypto && cd /build/sha3-crypto \
    && /src/wolfssl/configure $WOLF_COMMON --enable-armasm=sha3-crypto \
    && make -j"$(nproc)"

RUN set -eux; \
    for b in baseline sha3-crypto; do \
        mkdir -p /out/$b/bin; \
        cp /build/$b/wolfcrypt/benchmark/benchmark /out/$b/bin/; \
        cp /build/$b/wolfcrypt/test/testwolfcrypt  /out/$b/bin/; \
        cp /build/$b/examples/client/client        /out/$b/bin/ 2>/dev/null || true; \
        cp /build/$b/examples/server/server        /out/$b/bin/ 2>/dev/null || true; \
        cp /build/$b/wolfssl/options.h             /out/$b/; \
    done; \
    cp /src/wolfssl/README.md /out/ 2>/dev/null || true

# ---------------------------------------------------------------- runtime ---
FROM debian:bookworm-slim

LABEL org.opencontainers.image.title="wolfCrypt PQC benchmark (i.MX95)" \
      org.opencontainers.image.description="Live ML-KEM / ML-DSA benchmarks on the Cortex-A55 cluster" \
      org.opencontainers.image.vendor="wolfSSL Inc."

RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
        util-linux procps ca-certificates \
    && rm -rf /var/lib/apt/lists/*

COPY --from=builder /out/ /opt/wolfssl/
COPY entrypoint.sh /opt/wolfssl/
RUN chmod +x /opt/wolfssl/entrypoint.sh

WORKDIR /opt/wolfssl
ENTRYPOINT ["/opt/wolfssl/entrypoint.sh"]
