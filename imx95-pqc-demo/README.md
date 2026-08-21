# Post-quantum on both clusters of the NXP i.MX95

A two-pane demo on a single screen, running entirely on a Toradex SMARC iMX95:

| pane | what it shows |
|---|---|
| **left** | wolfCrypt ML-KEM / ML-DSA benchmarks in a container on the six Cortex-A55 cores, under Torizon OS |
| **right** | wolfBoot performing **ML-DSA-87 verified boot** of the Cortex-M7, with its console relayed to Linux |

The point of the pairing: the same post-quantum algorithms, at the same security
level, on two very different cores of one SoC - a Linux application cluster and
a bare-metal real-time core.

## Measured on hardware

Cortex-M7 at 800 MHz, DWT cycle counter, caches enabled:

| | wolfBoot text | verify + boot | at 800 MHz |
|---|---|---|---|
| ECDSA P-256 | 22,548 B | 1,741,288 cycles | 2.177 ms |
| **ML-DSA-87** | **21,608 B** | **4,201,179 cycles** | **5.251 ms** |
| ML-DSA-87, Zephyr payload | 21,608 B | 5,916,540 cycles | 7.396 ms |

Two results worth calling out. **Post-quantum verified boot costs 5.25 ms** -
2.4x the cycles of ECDSA P-256 and irrelevant against any real boot time. And
the **ML-DSA-87 bootloader is 940 bytes smaller than the ECDSA one**: ML-DSA
verification is SHAKE plus polynomial arithmetic and never pulls in the
big-integer math P-256 needs, so for a verify-only workload post-quantum can
cost *less* flash.

Verification also scales far better than payload size suggests - 1,004 B to
54,080 B is **54x the payload for 1.41x the verify**, because the lattice
signature check is a fixed cost and only the hash grows.

> The M7's I- and D-caches are **disabled out of reset**. Any i.MX95 M7
> benchmark taken without enabling them is wrong by one to two orders of
> magnitude - we measured SHA-256 at 1.1 MiB/s before enabling them and
> 29.2 MiB/s after.

## Layout

```
container/   wolfCrypt PQC benchmark container for the A55 cluster (left pane)
m7/          Zephyr RPMsg payload wolfBoot verifies and boots (right pane)
demo/        board-side orchestration and the two-pane renderer
gallery/     Torizon Demo Gallery submission: compose file and partner metadata
tools/       memtool - mmap-based /dev/mem access for the M7 console
charts/      the measured figures, as images
```

## Requirements

- Toradex SMARC iMX95 (or another i.MX95 board) running Torizon OS
- wolfBoot with the `imx95_m7` target
- A Zephyr workspace (4.4.0 or newer) and an `arm-none-eabi` toolchain
- Docker on the board (Torizon ships it)

## Building

**1. wolfBoot for the M7**, signing with ML-DSA-87:

```sh
cp config/examples/imx95-m7.config .config
make SIGN=ML_DSA ML_DSA_LEVEL=5 IMAGE_SIGNATURE_SIZE=4627 DEBUG_UART=1
```

`DEBUG_UART=1` is what makes wolfBoot write its verification log to the
shared-memory console the right pane reads.

**2. The Zephyr payload:**

```sh
cd m7 && ZEPHYR_BASE=~/zephyrproject/zephyr ./build.sh
```

Then sign it with the same key wolfBoot was built with:

```sh
IMAGE_HEADER_SIZE=12288 ML_DSA_LEVEL=5 ./tools/keytools/sign --ml_dsa --sha256 \
    m7/build/zephyr/payload.bin wolfboot_signing_private_key.der 1
```

**3. The benchmark container:**

```sh
cd container && WOLFSSL_REPO=/path/to/wolfssl ./build-aarch64.sh all && ./build-image.sh
```

## Running

```sh
BOARD=torizon@<board-ip> WOLFBOOT=/path/to/wolfboot ./demo/stage.sh
```

then on the board:

```sh
sudo bash ~/demo/demo-run.sh /dev/tty1     # /dev/tty1 is the HDMI console
```

Omit the argument to render on the current terminal instead.

### Starting it automatically

Because replaying the demo means power-cycling the board (see below), running it
by hand also means logging back in afterwards, over a network link that does not
always come up. Installing the unit makes cutting and restoring power the entire
replay:

```sh
sudo bash ~/demo/install-autostart.sh          # enable
sudo bash ~/demo/install-autostart.sh --off    # disable, restore the getty
```

It takes tty1 from `getty@tty1`, so the HDMI console is the demo rather than a
login prompt.

### Reading the verify log over RPMsg

The right pane does not need this, but the same log can be read as an ordinary
Linux tty:

```sh
sudo bash ~/demo/m7-rpmsg-log.sh
```

Order matters. The payload relays the whole console ring as soon as its endpoint
has a destination address, which the host supplies while binding the channel -
strictly before `/dev/ttyRPMSG*` exists. No reader can be attached for that first
pass, and the log it sends is discarded by a tty nobody has open. The payload
therefore treats any byte written to the tty as a request to rewind and send the
log again, so the script attaches a reader first and then pokes.

## Notes that will save you time

**The M7 starts once per Linux boot.** `echo stop > .../state` fails on this BSP
with "Interrupted system call" and the core stays running, so replaying the boot
means a real power cycle of the board - not a restart. Plan the demo around
that.

**The payload is not the upstream `openamp_rsc_table` sample, deliberately.**
That sample assumes Linux loaded *its* ELF: the remote declares vrings with
`da = FW_RSC_ADDR_ANY` and Linux writes the resolved addresses and the virtio
status back into the resource table it loaded. Here Linux loads **wolfBoot**, so
those values land in wolfBoot's table while the sample polls its own - and it
waits forever in `rproc_virtio_wait_remote_ready()`. This payload uses fixed
vring addresses matching wolfBoot's table and skips the wait, because there is
nothing to wait for: Linux registers virtio0 before it releases the core.

**The right pane reads the console ring directly, not `rpmsg_tty`.** wolfBoot
writes its log before any RPMsg endpoint exists, so the ring is the only source
that contains the verification output - and reading it needs no driver, no
endpoint binding and no module load.

**Nothing may sit unflushed in the page cache.** The replay beat is a hard power
cut, so a file written seconds earlier is simply gone after the next cycle - a
staged payload, or the autostart unit, silently reverts to what was there
before. `stage.sh` and `install-autostart.sh` both `sync`; verify a hand-copied
payload with `md5sum` rather than a timestamp.

**Torizon has no tmux**, and its rootfs is read-only OSTree, so there is nothing
to install. `demo/twopane.py` renders two fixed columns with a full redraw
instead.

**Do not quote the benchmark's "Cycles per byte" or "Cycles/op" columns on an
A55.** They derive from the 24 MHz generic timer rather than the 1.8 GHz core
clock and are wrong by roughly 75x. Use ops/sec and MB/s, or pass
`-freq 1800000000`. The demo renderer strips those columns for this reason.

## Support

wolfSSL is dual licensed under GPLv3 or a commercial license. Questions:
support@wolfssl.com
