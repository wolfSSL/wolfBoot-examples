#!/bin/bash

set -e

./build_qemu_fsp.sh
(cd linux-image && ./compile_linux.sh)
cp wolfboot-config ../wolfBoot/.config
cp fsp_m.bin fsp_s.bin fsp_t.bin ../wolfBoot/src/x86/fsp/machines/qemu/
cp linux-image/app.bin ../wolfBoot/
(cd ../wolfBoot && git submodule init)
(cd ../wolfBoot && git submodule update)
(cd ../wolfBoot && make keytools)
(cd ../wolfBoot && make x86_qemu_flash.bin)
qemu-system-x86_64 -m 256M -machine q35 -serial mon:stdio -nographic -pflash ../wolfBoot/x86_qemu_flash.bin
