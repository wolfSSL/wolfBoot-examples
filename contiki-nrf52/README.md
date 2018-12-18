# dtls-ota
Over-the-air firmware upgrade example for Nordic nRF52, using DTLSv1.2/UDP/IPv6/6LoWPAN over BLE.

This project is meant to demonstrate a firmware upgrade mechanism based on [wolfBoot secure bootloader](https://github.com/wolfssl/wolfBoot), powered by wolfSSL.

The bootloader expect the application to transfer the new firmware through a secure channel, store it in a partition on the flash memory and trigger an upgrade on the next reboot.

The application in this example uses DTLSv1.2 over Bluetooth Low-Energy (BLE) to request a new firmware image from a remote server. Once the transfer is complete, the target is rebooted into
the bootloader, which validates the new image and copies it to the active boot partition.


## Components
  - Bootloader: [wolfBoot](https://github.com/wolfssl/wolfBoot) by wolfSSL
  - OS:  [contiki](http://contiki-os.org/)
  - TLS: [wolfSSL](https://www.wolfssl.com) providing DTLS v1.2 support with ECC-based cipher
  - UDP/IPv6 stack: uIP
  - Application: Custom firmware upgrade contiki application with DTLS Client and wolfBoot integration

## Preparing the initial firmware

The flash memory on the nRF52 is divided as follows:

```
  - 0x000000 - 0x01efff : Reserved for Nordic SoftDevice binary
  - 0x01f000 - 0x02efff : Bootloader partition for wolfBoot
  - 0x02f000 - 0x056fff : Active (boot) partition
  - 0x057000 - 0x057fff : Unused
  - 0x058000 - 0x07ffff : Upgrade partition
```

Running `make` assembles the following images:
  - pre-built SoftDevice binary (downloaded from developer.nordicsemi.com)
  - wolfBoot compiled to run at address 0x01f000 and linked with the nrf52 HAL
  - Contiki-OS with automatic start-up of the firmware upgrade process, in a signed image that can be verified by wolfBoot during start-up

If it is the first time that `make` is executed, it will also:
  - Generate a new Ed25519 keypair, used to sign and verify the firmware images
  - Download a copy of Nordic SoftDevice binary from developer.nordicsemi.com

Running `make flash` will upload the three components into the respective partitions onto the target.

More information about wolfBoot upgrade mechanism can be found in the [wolfBoot](https://github.com/wolfSSL/wolfBoot) repository.

## Firmware upgrade

The directory [ota-server](ota-server) contains a small exaple DTLS v1.2 server that can be used to transfer a (signed) image to any client requesting a firmware upgrade.
To compile ota-server for the host system, simply run `make` within the directory.

In order to establish a layer-2 link with the target, the host must configure a 6loWPAN device on top of the system BLE support.

Before starting, a file mac.txt must be created inside the ota-server directory, containing the physical address of the BLE interface on the target.

To discover the address of the target, use `hcitool lescan` from the host. Among the discovered host, look for the target fingerprints:
```
00:22:99:CC:EE:88 Contiki nRF52dk
```

And create the file `mac.txt` accordingly:
```
00:22:99:CC:EE:88
```

The script `start.sh` contains all the instruction needed to create the connection and start the ota-server, listening on port 11111 for incoming DTLS sessions. It will also assign a site-local fixed IPv6 address that the DTLS client on the target uses to contact ota-server.

After the DTLS handshake, the server transmits the size of the firmware, and then the flash area content in chunks of 512B each.

When the transfer is complete, a flag is activated at the end of the flash area to notify wolfBoot of a pending upgrade.

After reboot, wolfBoot will copy the image from the secondary partition to the primary partition, to allow the new firmware to run, but only if the new firmware can be authenticated using the public Ed25519 key stored in the bootloader image. In all other cases, the upgrade is canceled and the old firmware can be started again.

## Successful upgrade: serial output from target

Below, an extract of the messages printed on the serial console of the target during and after the upgrade procedure:

```
OTA BLE Firmware upgrade, powered by Contiki + WolfSSL.
This firmware build: 1540927787
Client IPv6 address:
  fd00::xx:yy:zz
  fe80::xx:yy:zz
wolfSSL: Setting peer address and port
connecting to server...

Timeout!
Retrying...
Connected to OTA server.
Firmware size: 127236
Erase complete. Start flashing
RECV: 512/127236
RECV: 1024/127236
RECV: 1536/127236
RECV: 2048/127236
```
(cut prints of each datagram received)
```
RECV: 126464/127236
RECV: 126976/127236
RECV: 127236/127236
Closing connection.
Transfer complete. Triggering wolfBoot upgrade.
Rebooting...
OTA BLE Firmware upgrade, powered by Contiki + WolfSSL.
This firmware build: 1540927848
```

In case of success, the build number can be used to verify that the system has actually been upgraded by comparing the build numbers before the transfer and after the reboot.


## Copyright notice
ota-server example is Copyright (c) 2018 wolfSSL Inc., and distributed under the term of GNU GPL2.

dtls-ota embedded application and the ota-server example are Copyright (c) 2018 wolfSSL Inc., and distributed under the term of GNU GPL2, with a specific linking exception allowing to link against Nordic SoftDevice binary blob.

wolfBoot, wolfSSL (formerly known as CyaSSL) and wolfCrypt are Copyright (c) 2006-2018 wolfSSL Inc., and licensed for use under GPLv2.

Contiki OS and uIP are licensed under the terms of the 3-clause BSD license.

wolfBoot, wolfSSL (formerly known as CyaSSL) and wolfCrypt are Copyright (c) 2006-2018 wolfSSL Inc., and licensed for use under GPLv2.

See the documentation within each component subdirectory for more information about using and distributing this software.

