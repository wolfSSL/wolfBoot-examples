# i.MX95 demo: a technical walkthrough of the boot

This follows a single cold boot line by line, explaining what each stage is and what it hands to the next. It is written to be read next to a live console or the captured log.

The one-line summary: **wolfBoot is the first thing the boot ROM runs.** It occupies the slot U-Boot SPL normally holds, and there is no U-Boot anywhere in this chain.

## The chain

```
  power on
     |
  boot ROM            on-die, immutable. Reads the AHAB container set from eMMC.
     |
  ELE firmware        EdgeLock Enclave. The security subsystem. Container 0, image 1.
     |
  System Manager      runs on the Cortex-M33. Owns clocks, power domains, pinmux.
     |                Container 0, image 2.
  OEI                 Optional Executable Image. Trains DDR. Container 0, image 3.
     |
  wolfBoot stage 1    <-- replaces U-Boot SPL. Container 0, image 4, OCRAM 0x20480000.
     |                Reads eMMC, walks the container set, loads the next three.
  BL31                ARM Trusted Firmware, secure monitor, EL3. DRAM 0x8A200000.
     |
  OP-TEE              trusted OS, secure EL1. DRAM 0x8C000000.
     |
  wolfBoot BL33       verifies the Linux FIT with ML-DSA-87. DRAM 0x90200000.
     |
  Linux
```

The Cortex-M7 runs a second, independent wolfBoot that verifies a Zephyr image, started later by Linux.

## Where the root of trust sits

Worth being exact about, because the honest answer is not the same on a development module as on a production one.

The boot ROM is on-die and immutable, so it is the only thing in the chain that cannot be replaced. What it *enforces* depends on the part's lifecycle:

- **Open part, as modules ship.** The ROM parses the AHAB container set and checks each image against the SHA-384 stored in the container's image array. That is an integrity check, not an authenticity one: anyone who can write the boot medium can also write matching hashes. The signature block is present but empty - a 16-byte header with the SRK-table, certificate and signature offsets all zero - and the ROM does not check it.
- **Closed part.** With the OEM SRK hash burned into fuses and the lifecycle advanced, the ROM authenticates the container against those fuses and refuses anything it cannot verify. That is the hardware anchor.

The demo module is open. You can confirm it on any container:

```
container 0: tag=0x87 ver=0x02 len=672 images=5 sigblock_off=0x290
   sigblock tag=0x90 len=16 srk_off=0x0 cert_off=0x0 sig_off=0x0
```

There is also a simpler proof in this repo's own tooling: `ahab-replace-spl.py` swaps an image and only recomputes its SHA-384, and the board boots the result. That can only work where nothing is signed over the container header.

So on this board the cryptographic claim is **wolfBoot's**, not the ROM's: wolfBoot verifies the Linux FIT with ML-DSA-87, and the second wolfBoot on the Cortex-M7 verifies its Zephyr payload the same way. Both are real and both fail closed. What is missing underneath is the fused anchor.

**Closing that gap.** Signing the containers needs no fuses and is worth doing on its own - see *Signing the containers* in `BUILD.md`. Having the hardware enforce the signature does need them, and both steps are one-way: a wrong SRK hash, a lost private key, or a container that was never confirmed to boot while the part was open leaves a board with no recovery path, serial downloader included. That is a deliberate decision for a product, not something to do to a demo unit.

One forward-looking note: i.MX95's AHAB supports OEM hybrid ML-DSA SRKs, so the ROM-level anchor can itself be post-quantum rather than ECDSA alone. That makes a fully post-quantum chain from ROM to Linux possible on this silicon; this demo shows the wolfBoot half of it.

**Acronyms.** *AHAB* - Advanced High Assurance Boot, NXP's signed-container boot format. *ELE* - EdgeLock Enclave. *OEI* - Optional Executable Image, code the ROM runs and returns from. *BL31/BL33* - ARM Trusted Firmware boot-loader stages; BL31 is the secure monitor, BL33 the non-secure payload. *OCRAM* - on-chip RAM, available before DDR is trained. *FIT* - Flattened Image Tree, the kernel/DTB/initramfs bundle. *ML-DSA* - the NIST post-quantum signature standard, FIPS 204, formerly Dilithium.

## Stage 1 - what replaces U-Boot SPL

```
wolfBoot stage 1: NXP i.MX95 Cortex-A55
```

The first line on the console. On a stock module this would be `U-Boot SPL 2024.04`. The ROM has authenticated container 0, run the ELE firmware, started the System Manager on the M33, and let the OEI train DDR. It then loaded this image into OCRAM at `0x20480000` and jumped to it at EL3.

```
scmi: uSDHC1 clock 200000000 -> 400000000 Hz
scmi: uSDHC1 clock+pinmux up
```

The M33 System Manager owns the clock tree, so a stage running on the A55 asks for clocks over **SCMI** (System Control and Management Interface) rather than writing clock registers itself. The ROM leaves the eMMC controller clocked at 200 MHz; the driver's dividers are written against 400 MHz, so stage 1 sets the parent and rate before touching the controller.

```
emmc: ready, rca=0x1 part_config=0x50
stage1: eMMC boot1
```

The eMMC is up. `part_config` is EXT_CSD byte 179: `0x50` means the ROM boots from **boot1**. Stage 1 reads that same field back and points its own reads at the same partition, so it always reads the container set the ROM actually used.

```
stage1: image 0 -> 0x8A200000, 38912 bytes
stage1: image 1 -> 0x90200000, 64512 bytes
stage1: image 2 -> 0x8C000000, 593920 bytes
stage1: entering BL31 at 0x8A200000
```

The container walk. Nothing on the medium records where the second container begins - it is derived as the end of the first one, rounded up. Stage 1 parses the image array and copies each image to its destination: BL31, then wolfBoot BL33, then OP-TEE. Then it enters BL31, exactly as U-Boot SPL would.

## BL31 and OP-TEE

```
NOTICE:  BL31: v2.10.0  (release):lf-6.6.52-2.2.1
```

ARM Trusted Firmware takes EL3. It starts OP-TEE in the secure world, then drops to non-secure EL2 and enters BL33. These are the SoC vendor's own binaries, unchanged.

## wolfBoot as BL33

```
wolfBoot: NXP i.MX95 Cortex-A55 (BL33)
i.MX95 A55 handoff recon:
  CurrentEL:   EL2
  SCTLR_ELx:   0x0000000030C51835
    MMU=1 I$=1 D$=1
```

wolfBoot reports the machine state it was handed: exception level 2, MMU and both caches on. The caches matter - hashing a 56 MB image uncached would be roughly a hundred times slower, and the MMU is what lets wolfBoot use the A55's ARMv8 cryptographic instructions.

```
scmi: M7 powered, TCM ECC initialized
scmi: uSDHC2 clock+pinmux+power up
usdhc: high speed (50 MHz)
usdhc: SD card ready, rca=0xAAAA (high capacity)
```

wolfBoot powers the Cortex-M7 domain and scrubs its tightly-coupled memory so its ECC is valid, then brings up the second SD controller for the carrier card. Card power and the card-detect line are carrier GPIOs owned by the System Manager, so those go over SCMI too.

```
Reading MBR...
  MBR part 1: type=0x83, start=0x4000000, size=64MB
Checking primary OS image in 0,1...
Versions, A:1 B:0
Attempting boot from P:A
```

wolfBoot finds its A and B slots on the SD card and picks A, which holds version 1. B is empty. This is the A/B update machinery: a new image goes to B, and wolfBoot falls back if it fails to boot.

```
Boot partition: 0x9024C290 (sz 56343747, ver 0x1, type 0xB01)
Loading image from disk...done (4915 ms)
Checking image integrity...done (134 ms)
Verifying image signature...
info: ML-DSA 5 verify_signature: pubkey 2592, sig 4627
info: wc_MlDsaKey_Verify returned OK
```

**This is the point of the demo.** The FIT is read from the SD card, hashed, and its signature checked against the public key compiled into wolfBoot. The algorithm is ML-DSA at security level 5 - a 2592-byte public key and a 4627-byte signature. Only after that does wolfBoot hand off to Linux.

If the signature does not match, wolfBoot says `Error validating authenticity` and stops. It does not boot the image.

## Linux, and the second core

Linux comes up, and systemd starts the demo units. They launch the Cortex-M7 through `remoteproc` and stream both cores to the console. Lines are tagged by origin:

```
[M7 ] wolfBoot: NXP i.MX95 Cortex-M7
[M7 ] info: wc_MlDsaKey_Verify returned OK
[M7 ] *** Booting Zephyr OS build v4.4.0 ***
[A55] ML-DSA-87 verify   1719.904 ops/sec
```

The M7 is running its own wolfBoot, verifying a Zephyr image with the same algorithm. Both cores then run wolfCrypt benchmarks - ML-KEM key encapsulation and ML-DSA signatures - so the audience sees post-quantum cryptography both *verifying the boot* and *running as a workload*.

The run ends with:

```
 Demo complete. Both cores booted firmware verified with ML-DSA-87:
   Cortex-A55  wolfBoot -> Linux
   Cortex-M7   wolfBoot -> Zephyr
```

## Switching modes

The board carries two boot chains, one per eMMC boot partition, and you can switch between them to show the same hardware booting with and without wolfBoot in the SPL slot.

| | `PARTITION_CONFIG` | chain |
|---|---|---|
| **boot0** | `0x48` | stock Toradex: U-Boot SPL -> U-Boot -> Linux |
| **boot1** | `0x50` | wolfBoot stage 1 -> BL31 -> OP-TEE -> wolfBoot -> Linux |

```
export BOARD=torizon@<board-ip>
../tools/emmc-select-boot.sh 0    # stock
../tools/emmc-select-boot.sh 1    # wolfBoot
```

Then power-cycle. The setting lives in EXT_CSD and survives power loss, so the board stays on whichever side you selected.

The tell is the first console line: `U-Boot SPL` on boot0, `wolfBoot stage 1` on boot1. The rootfs is the same eMMC filesystem either way, so userspace is identical and only the boot chain differs.

**The boot jumper cannot do this.** On this module SD boot is enabled by SoC fuses, not by the carrier's BOOT_SEL strap, so BOOT_SEL cannot select the boot medium. Leave it alone.

## If something does not come up

Check the first console line. No output at all usually means the board is not powered or the console cable is on the wrong header - the demo console is SMARC SER1 at 115200 8N1.

`wolfBoot: PANIC!` after `Error validating authenticity` means the image on the SD card was not signed with the key compiled into this wolfBoot. That is a mismatched pair, not a corrupt card.

If `boot1` does not start at all, select `boot0` and power-cycle; the stock chain is untouched. If the board will not boot either way, hold the recovery button and power on with a USB-C cable to the recovery port, then run `./uuu uuu-partconf.auto` from the Toradex Easy Installer recovery directory - it resets the boot partition to `boot0` from RAM without writing storage.
