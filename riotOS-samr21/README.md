# fw-update
Firmware update example on RIOT-OS, using serial console to transfer the update

This project is meant to demonstrate a firmware upgrade mechanism based on [wolfBoot secure bootloader](https://github.com/wolfssl/wolfBoot), powered by wolfSSL.

The bootloader expect the application to transfer the new firmware, store it in the update partition on the flash memory and trigger an upgrade on the next reboot.

The application in this example uses USB CDC-ACM to download a new firmware image from the host machine. Once the transfer is complete, the target is rebooted into
the bootloader, which validates the new image and copies it to the active boot partition.


## Components
  - Bootloader: [wolfBoot](https://github.com/wolfssl/wolfBoot) by wolfSSL
  - OS:  [RIOT-OS](https://riot-os.org/)
  - Application: Custom firmware update RIOT-os application that downloads the update through USB CDC-ACM

## Preparing the initial firmware

The flash memory on the samR21 is divided as follows:

```
  - 0x000000 - 0x007fff : Bootloader partition for wolfBoot
  - 0x008000 - 0x022fff : Active (boot) partition
  - 0x023000 - 0x03efff : Upgrade partition
  - 0x03e000 - 0x03e0ff : Swap sector
```

Running `make` assembles the following images:
  - wolfBoot compiled to run on SamR21
  - RIOT-OS with automatic start-up of the firmware update process, in a signed image that can be verified by wolfBoot during start-up

Running `make wolfboot-flash` will upload the two components into the respective partitions onto the target.

More information about wolfBoot upgrade mechanism can be found in the [wolfBoot](https://github.com/wolfSSL/wolfBoot) repository.

## Firmware update

The directory [fw-update-server](fw-update-server) contains a small exaple DTLS v1.2 server that can be used to transfer a (signed) image to any client requesting a firmware upgrade.
To compile fw-update-server for the host system, simply run `make` within the directory.

To run the fw-update-server, simply run `./server` followed by the path of the signed firmware to transfer. The firmware previously compiled and signed with `make` can be found in `fw-update/bin/samr21-xpro/fw-update.bin.v5.signed`.

When launched, the server transmits the size of the firmware, and then the flash area content in chunks of 16B each.

When the transfer is complete, a flag is activated at the end of the flash area to notify wolfBoot of a pending upgrade (using `wolfBoot_update_trigger()`)

After reboot, wolfBoot will copy the image from the secondary partition to the primary partition, to allow the new firmware to run, but only if the new firmware can be authenticated using the public Ed25519 key stored in the bootloader image. In all other cases, the upgrade is canceled and the old firmware can be started again.


## Copyright notice
fw-update-server example is Copyright (c) 2018 wolfSSL Inc., and distributed under the term of GNU GPL2.

fw-update embedded application for RIOT-os is Copyright (c) 2018 wolfSSL Inc., and distributed under the term of GNU GPL2.

wolfBoot, wolfSSL (formerly known as CyaSSL) and wolfCrypt are Copyright (c) 2006-2018 wolfSSL Inc., and licensed for use under GPLv2.

RIOT-OS is distributed under the term of GNU LGPL v2.1.

See the documentation within each component subdirectory for more information about using and distributing this software.

