## BLE Firmware update with wolfBoot on nRF52

### Components

 - BLE GATT server application, running RIOT-OS
 - Python firmware update client, running on GNU/Linux

### Usage

 - Compile wolfBoot and the GATT server application using:

```
make
```



 - Connect the nRF52840 Development Kit and use the following command to upload the
   bootloader and the version 1 of the signed application to the board:

```
make flash
```


 - Prepare an update package using:

```
make update
```

- You can check the current version running on the target, either from a USB-serial console on `/dev/ttyACM0` by typing `info` into the shell, or 
by reading the BLE GATT characteristics of the device.

- Use the fwupdate.py python application to transfer the update via BLE:

```
linux-bluez/fwupdate.py nrf52-gatt-service/bin/nrf52840dk/nrf52-gatt-service_v2_signed.bin
```


