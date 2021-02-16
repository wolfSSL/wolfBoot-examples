# Freescale-K64F-FreeRTOS-SCP-Update

Firmware update example on FreeRTOS, using wolfSSH

This project is meant to demonstrate a firmware upgrade mechanism based on [wolfBoot secure bootloader](https://github.com/wolfssl/wolfBoot), powered by wolfSSL.

The bootloader expect the application to transfer the new firmware, store it in the update partition on the flash memory and trigger an upgrade on the next reboot.

The application in this example uses wolfSSH to create a simple SCP server.
A SCP client can upload a new firmware image from the host machine. Once the transfer is complete, the target is rebooted into
the bootloader, which validates the new image and copies it to the active boot partition.


## Components
  - Bootloader: [wolfBoot](https://github.com/wolfssl/wolfBoot) by wolfSSL
  - SSH library: [wolfSSH](https://github.com/wolfssl/wolfSSH) by wolfSSL
  - OS:  [freeRTOS](https://www.freertos.org/)
  - TCP/IP stack: PicoTCP

## Preparing the initial firmware

The flash memory on the FRDM-K64F is divided as follows:

```
  - 0x000000 - 0x009fff : Bootloader partition for wolfBoot
  - 0x00a000 - 0x083fff : Active (boot) partition
  - 0x084000 - 0x0fdfff : Upgrade partition
  - 0x0ff000 - 0x0fffff : Swap sector
```

### Prerequisites

Kinetis SDK `FRDM-K64F` (can be downloaded from NXP website).

### Compiling

The path to the Kinetis SDK directory must be passed to make through the `KINETIS=` variable.

Running `make KINETIS=/path/to/FRDM-K64F` assembles the following images:
  - wolfBoot compiled to run on FRDM-K64F
  - freeRTOS with automatic start-up of the firmware update process, in a signed image that can be verified by wolfBoot during start-up

The image `factory.bin` contains the bootloader and version 1 of the firmware. It can be directly transferred to the FRDM-K64F using OpenSDA functionalities (e.g. copying to the USB storage)

More information about wolfBoot upgrade mechanism can be found in the [wolfBoot](https://github.com/wolfSSL/wolfBoot) repository.

## Firmware update

Once the factory image is installed on the board and running, the board can be reached at the IP address 192.168.178.211.

The public key allowed by the board is the ECDSA key provided in the wolfSSH example with username 'hansel'.

First of all, change the permission of this example private key distributed with wolfSSH. A SCP client may refuse to use SSH keys that are publicly readable. This can be done via:

```
chmod 0700 wolfssh/keys/hansel-key-ecc.pem
```

To initiate a firmware update, transfer the file using scp. Use the '-i' option to force public-key based authentication using hansel's private key as follows:

```
scp -i wolfssh/keys/hansel-key-ecc.pem image_v2_signed.bin hansel@192.168.178.211:/update.bin
```

After the update, wait 30-40 seconds. The board should now reboot into version 2 of the firmware.


## Copyright notice
This example is Copyright (c) 2021 wolfSSL Inc., and distributed under the term of GNU GPL2.

FreeRTOS Kernel is Copyright (C) 2021 Amazon.com, Inc. or its affiliates.  All Rights Reserved. Distributed freely as specified by the MIT Open source license.

Some NXP/Freescale specific drivers used in this example are Copyright (c) 2015, Freescale Semiconductor, Inc., Copyright 2016-2017 NXP, All rights reserved. Distributed freely as specified by BSD-3-Clause License.

PicoTCP is Copyright (c) 2012-2017 Altran Intelligent Systems. Some rights reserved. Distribute freely as specified by the GPL license.

wolfBoot, wolfSSH, wolfSSL (formerly known as CyaSSL) and wolfCrypt are Copyright (c) 2021 wolfSSL Inc., and licensed for use under GPLv2.

See the documentation within each component subdirectory for more information about using and distributing this software.

