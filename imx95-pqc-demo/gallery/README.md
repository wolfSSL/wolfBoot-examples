# wolfCrypt Post-Quantum Benchmarks - Torizon Demo Gallery entry

Submission package for the [Torizon Demo Gallery](https://www.torizon.io/demo-gallery),
prepared against the [partner guidelines](https://developer.toradex.com/torizon/application-development/demo-gallery/demo-gallery-partner-guidelines/).

## Demo name

wolfCrypt Post-Quantum Benchmarks

## Description

Live NIST post-quantum cryptography benchmarks on the Arm cores of a Torizon
module, running continuously on stock Torizon OS. The demo measures ML-KEM (FIPS 203)
and ML-DSA (FIPS 204) against the classical ECDSA, ECDHE and RSA they replace,
and prints each result as it completes.

The headline it demonstrates is counter-intuitive: post-quantum key
establishment is **faster** than the classical cryptography it replaces on this
part. ML-KEM-768 encapsulation runs at 17,112 ops/sec against 2,865 for an
ECDHE P-256 agreement, a 6x speedup, and ML-DSA-44 verifies 1.7x faster than
ECDSA P-256.

## Value proposition

Anyone shipping a connected device has to migrate to post-quantum cryptography
for CRA and IEC 62443 timelines. The usual assumption is that this costs
performance on embedded silicon. This demo shows, on real hardware rather than
in a datasheet, where that assumption holds and where it does not, so an
integrator can plan a migration with measured numbers.

## Expected behavior

On start the container prints a banner naming the wolfSSL version, the selected
build and the CPU features it detected, then marks the cycle `[RUNNING]` with a
timestamp, runs the benchmark set printing one line per algorithm and operation,
and closes the cycle with `[OK]` or `[FAILED]`. It pauses briefly and repeats,
so the screen is never static and the current state is always legible at a
glance rather than inferred from output scrolling.

Results are also written to `/results` in a named volume for later collection.

## Hardware

| | |
|---|---|
| Verified on | Toradex SMARC iMX95 Hexa 8GB (PN 00961100) on the Toradex SMARC Development Board |
| Expected to run on | any Arm64 Toradex module running Torizon OS |
| Torizon OS | 7.7.0 and newer |
| Architecture | arm64 / aarch64 |
| Peripherals | none required |
| Display | optional; output is a text console, readable over ssh or on HDMI |

The image contains no i.MX95-specific code. It is plain aarch64 and selects its
wolfSSL build from the CPU features the host reports at runtime, so it should
run unchanged on other Arm64 modules; the numbers will differ with the core and
clock. The i.MX95 is the part we have measured, and the only one we claim.

No carrier-specific hardware, no peripherals to connect, and no user-specific
configuration are needed. Everything is optional tuning through the environment
variables documented in `docker-compose.yml`, including `SOC_LABEL`, which names
the board in the banner (a container cannot normally read
`/proc/device-tree/model`).

## Deployment

```sh
docker compose up -d
```

`restart: always` brings it back after provisioning and after every reboot.

## Container image

- Registry: Docker Hub, `wolfssl/wolfcrypt-pqc`
- Tags: a pinned version (`1.0.0`) and `latest`, pushed together
- Platform: `linux/arm64`
- Base image: `debian:bookworm-slim`
- Size: about 129 MB
- Built from `../container/`, which cross-compiles wolfSSL for aarch64 on the
  native build platform rather than under emulation

Publish with `./publish.sh` (`VERSION=1.0.0 ./publish.sh` after `docker login`).
Docker Hub repositories are private by default; the Gallery needs this one set
to public once, in the repository's settings, after the first push.

Two wolfSSL builds ship in the image: a NEON baseline that runs on any
Cortex-A55, and one using the Armv8.2 SHA-3 instructions. The entrypoint reads
`/proc/cpuinfo` and picks the safe one at runtime, because FEAT_SHA3 is optional
in the architecture and running the SHA-3 build without it faults. On the i.MX95
that resolves to the baseline build.

## External dependencies

None beyond the base image. wolfSSL is compiled from source into the image and
linked statically, so there is nothing to install on the host and nothing
pulled at runtime.

## Licensing

wolfSSL is dual licensed under **GPLv3** or a commercial license from wolfSSL
Inc. Everything in this image is redistributable and publicly demonstrable
under GPLv3. The Debian base image carries its own upstream licenses.

## Links

- wolfSSL: https://www.wolfssl.com
- Source for this demo: https://github.com/wolfSSL/wolfBoot-examples
- Contact: facts@wolfssl.com

## Note on the Cortex-M7 half

The full demo pairs these benchmarks with wolfBoot performing ML-DSA-87 verified
boot on the i.MX95's Cortex-M7. That half is deliberately **not** part of this
Gallery entry: it requires wolfBoot flashed to the module and privileged access
to `/dev/mem`, so it cannot be plug-and-play on an unmodified Torizon OS image.
It is documented in the parent directory for anyone who wants to reproduce it.
