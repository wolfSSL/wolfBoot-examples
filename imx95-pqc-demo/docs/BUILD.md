# Building the i.MX95 post-quantum boot demo

This builds a Toradex SMARC i.MX95 that boots with **wolfBoot in place of U-Boot SPL**, verifies Linux with ML-DSA-87 on the Cortex-A55, and runs a second wolfBoot on the Cortex-M7 that verifies a Zephyr image the same way.

Everything here is a direct command. Where a script is used it lives in a public repository and is named by its path in that repository.

## What you need

**Hardware.** A Toradex SMARC iMX95 module on its carrier, a USB serial cable on SMARC SER1, a microSD card, and a way to power-cycle the board. A USB-C cable to the recovery port is worth having before you start - see *Recovery* at the end.

**Repositories.**

```
git clone https://github.com/wolfSSL/wolfBoot
git clone https://github.com/wolfSSL/wolfBoot-examples
git clone https://github.com/nxp-imx/imx-mkimage
export TOOLS=$PWD/wolfBoot-examples/imx95-pqc-demo/tools
```

Every command below runs from that directory - the one holding the three clones - and `$TOOLS` is where this demo's scripts live.

wolfBoot keeps wolfCrypt in a submodule, and nothing builds without it:

```
cd wolfBoot
git submodule update --init lib/wolfssl
```

The Cortex-A55 target is not on `master` yet - it is [PR #888](https://github.com/wolfSSL/wolfBoot/pull/888). Until that merges, `config/examples/imx95-a55.config`, `hal/imx95_a55.c` and the stage 1 come from its branch. The Cortex-M7 target in step 7 is already on `master`.

```
git fetch origin pull/888/head:imx95_a55_pr
git checkout imx95_a55_pr
git submodule update --init lib/wolfssl
```

**Toolchains.** `aarch64-none-elf-` (or `aarch64-linux-gnu-`) for the A55, `arm-none-eabi-` for the M7. On Debian or Ubuntu: `apt install gcc-aarch64-linux-gnu gcc-arm-none-eabi`.

**From the running board, once.** You need the SoC's own boot images - the EdgeLock Enclave (ELE) firmware, the M33 System Manager, and the Optional Executable Image (OEI) that trains DDR. They are not redistributable and not rebuilt here; take them from the module as shipped:

```
ssh <board> "sudo dd if=/dev/mmcblk0boot0 bs=1M count=32" > boot0.bin
```

`boot0.bin` is your base image and your fallback. Keep it.

ARM Trusted Firmware (BL31) and OP-TEE are in there too, so you do not need to build or source them separately. Pull everything out of that one file:

```
python3 $TOOLS/ahab-extract.py boot0.bin -o vendor/
```

```
container 0 at 0x8000, 5 image(s), 817152 bytes
  [0] empty marker at +0x75800
  [1] 0x1FFC0000    321536 bytes  Cortex-M33 image (System Manager or OEI)
       -> vendor/m33.bin
  [2] 0x1FFC0000    129024 bytes  Cortex-M33 image (System Manager or OEI)
       -> vendor/m33_c0i2.bin
  [3] 0x20480000    141312 bytes  A55 image in OCRAM (U-Boot SPL, or wolfBoot stage 1)
       -> vendor/spl.bin
  [4] empty marker at +0xc7800
container 1 at 0xcf800, 3 image(s), 1650688 bytes
  [0] 0x8A200000     38912 bytes  ARM Trusted Firmware BL31 (secure monitor, EL3)
       -> vendor/bl31.bin
  [1] 0x90200000    968704 bytes  BL33 (U-Boot proper, or wolfBoot)
       -> vendor/bl33.bin
  [2] 0x8C000000    593920 bytes  OP-TEE (trusted OS, secure EL1)
       -> vendor/tee.bin
```

Of those, `vendor/bl31.bin` and `vendor/tee.bin` are what the rest of this guide consumes; the others are kept because they document what the container holds. Images are named by load address, because those are fixed on this SoC. The two M33 images share a load address - the OEI runs and returns before the System Manager is loaded over it - so the second is written with an index suffix. The empty markers are zero-length end-of-list entries the ROM expects, and are why replacing the SPL slot does not move container 1.

## 1. Build mkimage

```
cd imx-mkimage
make bin
export MKIMAGE=$PWD/mkimage_imx8
```

## 2. Build wolfBoot for the A55

wolfBoot runs twice on the A55: as **BL33**, where ARM Trusted Firmware hands it control and it verifies and boots Linux; and as **stage 1**, in the slot U-Boot SPL normally occupies.

```
cd wolfBoot
cp config/examples/imx95-a55.config .config
make                                      # BL33 -> wolfboot.bin
make -C stage1 DISK_EMMC=1 DISK_SDCARD=0 loader_stage1.bin
```

The first build also generates a signing key (`wolfboot_signing_private_key.der`) and compiles the matching public key into wolfBoot's keystore. **Copy that key somewhere outside the tree.** `make keysclean` regenerates it, and a keystore that no longer matches your signed images means the board stops booting with an authenticity error.

```
cp wolfboot_signing_private_key.der ~/keys/imx95-demo-signing-key.der
```

To reuse an existing key instead of generating one, point the build at it. `USER_PRIVATE_KEY` and `USER_PUBLIC_KEY` take precedence over the generated key, and the build then never runs the key generator at all:

```
python3 -c "d=open('/path/to/key.der','rb').read(); open('/tmp/pub.der','wb').write(d[-2592:])"
make USER_PRIVATE_KEY=/path/to/key.der USER_PUBLIC_KEY=/tmp/pub.der
make -C stage1 DISK_EMMC=1 DISK_SDCARD=0 loader_stage1.bin \
    USER_PRIVATE_KEY=/path/to/key.der USER_PUBLIC_KEY=/tmp/pub.der
```

Both must be given together; the build errors out if only one is set, or if either file is missing. The private key is the 7488-byte DER as `keygen` writes it - 4896 bytes of private key followed by the 2592-byte public key, which is where the one-liner above takes the public half from.

Prefer this over importing with `keytools/keygen -i`. That route works, but it leaves the tree with a keystore and no matching private key, so the next plain `make` tries to generate a fresh key and stops on `Are you sure you want to generate a new key and overwrite the existing key ?`. Answering yes there replaces the keystore your signed images were built against, and the board stops booting with an authenticity error. For the same reason, do not reach for `make keysclean` to get out of that state: it deletes every `*.der` in the tree root, the extracted public key included.

## 3. Build and sign the Linux FIT

The payload is a FIT (Flattened Image Tree) holding the kernel, device tree and initramfs. Take the three components off the running board - they are whatever the distribution installed - and bundle them:

```
scp <board>:/boot/ostree/torizon-*/vmlinuz-* .
scp <board>:/boot/ostree/torizon-*/initramfs-* .
scp <board>:/boot/ostree/torizon-*/dtb/imx95-toradex-smarc-dev.dtb .
gunzip -c vmlinuz-* > Image
```

The kernel in `/boot` is gzip-compressed; the FIT wants the flat `Image`. Write `fit.its` describing the three:

```
/dts-v1/;
/ {
    description = "Kernel FIT for wolfBoot verified boot";
    #address-cells = <1>;
    images {
        kernel {
            data = /incbin/("Image");
            type = "kernel";  arch = "arm64";  os = "linux";
            compression = "none";
            load = <0xB2000000>;  entry = <0xB2000000>;
        };
        fdt {
            data = /incbin/("imx95-toradex-smarc-dev.dtb");
            type = "flat_dt";  arch = "arm64";  compression = "none";
            load = <0xB8000000>;
        };
        ramdisk {
            data = /incbin/("initramfs-6.6.142-7.7.0.img");
            type = "ramdisk";  arch = "arm64";  os = "linux";
            compression = "none";
        };
    };
    configurations {
        default = "conf";
        conf { kernel = "kernel"; fdt = "fdt"; ramdisk = "ramdisk"; };
    };
};
```

The load addresses put the kernel and device tree in DRAM clear of the regions BL31, OP-TEE and wolfBoot occupy. Then build it with `mkimage` from `u-boot-tools`:

```
mkimage -f fit.its fitImage
```

The FIT records a build timestamp, so two builds of identical inputs differ in three bytes. Set `SOURCE_DATE_EPOCH` if you want a reproducible result:

```
SOURCE_DATE_EPOCH=$(date +%s) mkimage -f fit.its fitImage
```

Now sign it with the same key wolfBoot was built with:

```
ML_DSA_LEVEL=5 IMAGE_HEADER_SIZE=12288 \
  wolfBoot/tools/keytools/sign --ml_dsa --sha256 \
      fitImage wolfBoot/wolfboot_signing_private_key.der 1
```

This writes `fitImage_v1_signed.bin`: the image with a 12288-byte wolfBoot header prepended. The `1` is the version; wolfBoot refuses to roll back to a lower one.

Note that `SOURCE_DATE_EPOCH` makes the FIT reproducible but not the signed image. ML-DSA signing is randomized, so signing the same FIT twice gives two different 4627-byte signatures and two different file hashes. Comparing a rebuild against a known-good signed image means comparing the payload past the 12288-byte header; the headers legitimately differ.

## 4. Assemble the boot image

The i.MX95 boot ROM reads a set of **AHAB** (Advanced High Assurance Boot) containers from the boot medium. There are two:

- **Container 0** at offset `0x8000` - ELE firmware, System Manager, OEI, and one A55 image loaded into OCRAM at `0x20480000`. That last slot is U-Boot SPL on a stock module, and is what wolfBoot takes over.
- **Container 1**, immediately after - BL31, BL33 and OP-TEE.

Nothing records where container 1 starts. It is the end of container 0 - the furthest of its header, its images and its signature block - rounded up to 1 KiB. On this module that lands at `0xcf800`.

Build container 1 with wolfBoot as BL33, splicing it into a copy of `boot0.bin`:

```
MKIMAGE=$MKIMAGE $TOOLS/ahab-pack-container2.sh \
    boot0.bin vendor/bl31.bin wolfBoot/wolfboot.bin vendor/tee.bin boot-wolfboot.bin
```

Then replace the SPL slot in container 0 with the stage 1:

```
python3 $TOOLS/ahab-replace-spl.py \
    boot-wolfboot.bin wolfBoot/stage1/loader_stage1.bin boot-final.bin
```

Each image entry carries a **SHA-384** over its payload, so that script recomputes the hash for the slot it replaces; the ROM rejects the container otherwise. It leaves container 0's other images and all of container 1 untouched, and because the SPL slot is followed by a zero-length end marker at a fixed offset, the container's size does not change and container 1 does not move.

### Signing the containers

The containers built above are unsigned, which is how the modules ship: the signature block is a 16-byte header with its SRK-table, certificate and signature offsets all zero. On an open part the ROM checks each image against the SHA-384 in the container and nothing more, so the boot is integrity-checked but not authenticated. `BOOT-WALKTHROUGH.md` covers what that does and does not give you.

Signing needs no fuses. NXP's Code Signing Tool builds an AHAB PKI tree with `ahab_pki_tree.sh`, and `srktool --ahab_ver` emits both the SRK table that goes in the container and `SRK_1_2_3_4_fuse.bin`, the hash that *would* be burned. Nothing in that flow burns anything. Sign with a CSF whose `Offsets` are the container offset and its signature-block offset, both of which `imx-mkimage` prints while building.

Two things to know before trying it here. The signature block has to be big enough to hold the SRK table, the certificate and the signature, so a container has to be *built* for signing rather than patched afterwards - the 16-byte stub in a stock image leaves no room, and `ahab-replace-spl.py` deliberately preserves the existing layout. And a signed container proves nothing on an open part: the ROM will boot it either way, so the only checks available are structural - diff signed against unsigned, and confirm the signature block still ends before the first payload offset.

Burning the SRK hash and closing the part are irreversible, and a part closed against a container that was never confirmed to boot is unrecoverable, serial downloader included.

### Reproducibility

Every image in both containers comes out byte-identical to the ones running on our demo board - the two M33 images, the stage 1, BL31, BL33 and OP-TEE - which is the check that the path above is complete: everything came from that single `dd`, plus wolfBoot. Extract your result with `ahab-extract.py` and compare the files it writes.

Compare the extracted images, not the raw assembled file. Splicing a smaller BL33 into the slot a larger U-Boot occupied leaves the tail of that slot holding whatever the base `boot0.bin` had there. Those bytes sit past the declared image length, so the ROM never reads them, but they differ between base images and make a plain `cmp` of the whole file misleading.

## 5. Write the SD card

wolfBoot BL33 reads the signed FIT from the carrier SD. Partition it once - three primary partitions, the first two being wolfBoot's A and B slots:

```
sudo sfdisk /dev/sdX <<'EOF'
label: dos
start=131072, size=131072, type=83
start=262144, size=262144, type=83
start=524288, size=262144, type=83
EOF
sudo dd if=fitImage_v1_signed.bin of=/dev/sdX2 bs=1M conv=fsync
```

wolfBoot numbers partitions from zero, so its "partition A" is the second MBR entry, `/dev/sdX2`.

## 6. Flash the board

The eMMC has two boot partitions and EXT_CSD selects which one the ROM reads. Write the one you are **not** currently booting from, so the other stays as a way back:

```
export BOARD=torizon@<board-ip>
$TOOLS/emmc-write-boot.sh boot-final.bin 1
$TOOLS/emmc-select-boot.sh 1
```

Power-cycle. Both scripts verify by reading back and neither embeds a password; use an ssh key and sudo on the target.

## 7. Build the Cortex-M7 side

```
cd wolfBoot
cp config/examples/imx95-m7.config .config
make
```

The M7 image is loaded by Linux `remoteproc` rather than by the ROM, so it is not part of the AHAB container set. Staging and start-up are covered by `wolfBoot-examples/imx95-pqc-demo/demo/m7-start.sh`, and the Zephyr application it verifies is built from `wolfBoot-examples/imx95-pqc-demo/m7/zephyr-app/`.

The Zephyr application links wolfCrypt's own benchmark, which needs a wolfSSL new enough to zero the ML-KEM key objects in `bench_mlkem()`. Without that, `bench_mlkem_keygen()` calls `wc_MlKemKey_Free()` on memory that was only allocated, frees whatever pointers it happened to contain, and the core takes a bus fault a moment after the benchmark header prints.

Two things bite when reloading M7 firmware. The core cannot be stopped once wolfBoot's `main()` is spinning - the kernel logs `Not in wfi` and `can't stop rproc: -4` - so a reload silently leaves the previous firmware running; power-cycle instead. And after copying firmware to the board, run `sync` before cutting power, or the write sits in page cache and the board comes up on the old image with a filename and size that look right.

## 8. Install the demo

```
scp -r ../demo <board>:~/demo
ssh <board> "sudo bash ~/demo/install-autostart.sh"
```

That installs two systemd units: one starts the Cortex-M7 through `remoteproc`, the other streams both cores' output to the serial console on every boot. `--off` disables the demo and gives the login prompt back.

### DisplayPort as a second view

```
ssh <board> "sudo bash ~/demo/install-autostart.sh --display"
```

`--display` adds a third unit, `wolfssl-display.service`, which renders the same two panes onto a DisplayPort panel. It is additive rather than an alternative: the renderer starts nothing, it only follows the container logs and the Cortex-M7 console ring that the serial unit brings up, so the screen and the console show the same run. It also disables the older combined `wolfssl-demo.service`, which both renders and starts the demo - running that one alongside these gives two renderers fighting for the panel.

Torizon has no fbdev emulation, so the renderer owns the panel through KMS as the sole DRM master. The unit takes tty1 from the getty with `Conflicts=`, and `demo-display-fg.sh` unbinds the framebuffer console and stops Torizon's pairing overlay before it starts.

No font is committed here. `drmfb.py` uses `$DRM_FONT` if set, then a `font.ttf` placed next to it, then the first monospace TrueType face it finds among the usual distribution paths (Noto Sans Mono, DejaVu Sans Mono, Liberation Mono). With none of those it falls back to a built-in 8x8 bitmap, which is legible but blocky.

Torizon OS ships no fonts and has no package manager - the filesystem is an OSTree deployment - so on this board the search finds nothing and a font has to be staged by hand. Pillow is already installed. Any monospace TrueType face works; the demo was shown with Noto Sans Mono, which is under the SIL Open Font License 1.1 and available from <https://fonts.google.com/noto/specimen/Noto+Sans+Mono>:

```
scp NotoSansMono-Regular.ttf <board>:~/demo/font.ttf
```

## Recovery

`boot0` is never written by any of the above, so it is always the way back. If an image in `boot1` fails to start, hold the recovery button (**B4**) and power the board on, with a USB-C cable from the recovery port to your host. The boot ROM's serial downloader takes over - it lives in ROM and needs nothing on the eMMC, so replacing SPL does not put recovery at risk. Then, from Toradex's Easy Installer package:

```
cd <tezi-package>/recovery
./uuu uuu-partconf.auto
```

That boots a recovery U-Boot entirely in RAM and sets the boot partition back to `boot0` without writing storage. Release the button and power-cycle.

The carrier's BOOT_SEL jumper does **not** need changing, and should be left alone: on this module SD boot is enabled by SoC fuses rather than by the strap, so BOOT_SEL cannot select it.
