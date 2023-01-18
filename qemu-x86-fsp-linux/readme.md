# Running Wolfboot on QEMU x86

This readme describes the steps to run wolfboot as the bootloader on QEMU x86
using the Intel Firmware Support Package (FSP). In this example wolfboot will run a
simple self-contained Linux kernel image. The steps described can be run
automatically with the script `./build_and_run_example.sh`.

## Build FSP binary blobs

The Intel FSP provides three binary blobs that contain the code to initialize
the memory and the silicon. They are architecture dependent and provided by
Intel. The EDKII project provides blobs that can be used with QEMU q35
chipset emulation. The script `./build_qemu_fsp.sh` will clone the EDKII
repository and create the FSP objects needed. It will also rebase the objects
with the values taken from the file `wolfboot-config`. The same file will be
used as the wolfboot configuration file (`.config`) in this example.

## Compile a self contained Linux image

In this example wolboot will boot a self-contained Linux image obtained using
buildroot. Use the script `./linux-image/compile-linux.sh` to automatically
build the image. The `bzImage` will be copied in the same directory of the
script with the name `app.bin`.

## Clone and setup the wolfboot repository

clone and setup wofboot repository and keytools:
```bash
git clone https://github.com/wolfSSL/wolfBoot.git
(cd wolfBoot && git submodule init)
(cd wolfBoot && git submodule update)
(cd wolfBoot && make keytools)
```
copy the FSP binaries with

```bash
cp fsp_m.bin fsp_s.bin fsp_t.bin wolfboot/src/x86/fsp/machines/qemu/
```
The path to the FSP binaries is defined the wolfboot configuration.

copy the linux image into the wolfboot root directory as well:
```bash
cp app.bin wolfboot/
```

## Build wolfboot
```bash
cp wolfboot-config wolfboot/.config
make -C wolfboot x86_qemu_flash.bin
```

## Run QEMU
```bash
qemu-system-x86_64 -m 256M -machine q35 -serial mon:stdio -nographic -pflash wolfboot/x86_qemu_flash.bin
```
