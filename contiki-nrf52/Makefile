NRF52_SDK_ROOT=$(PWD)/nrf5x-softdevice
CROSS_COMPILE:=arm-none-eabi-
OBJCOPY:=$(CROSS_COMPILE)objcopy
JLINK_OPTS = -Device NRF52 -if swd -speed 1000
DTLS_OTA=$(PWD)/dtls-ota
BOOT_IMG:=$(DTLS_OTA)/dtls-ota
BOOT_ELF:=$(DTLS_OTA)/dtls-ota.nrf52dk
WOLFBOOT:=$(PWD)/../wolfBoot
WOLFBOOT_BIN:=$(WOLFBOOT)/wolfboot.bin
DEBUG?=0

all: $(DTLS_OTA)/dtls-ota-signed.bin

.contiki_patched:
	patch -p0 < contiki-nrf52-softdevice-wolfBoot.patch
	touch .contiki_patched

$(BOOT_ELF): nrf5_iot_sdk_3288530.zip $(WOLFBOOT_BIN) .contiki_patched
	echo "**"
	make -C $(DTLS_OTA) TARGET=nrf52dk NRF52_SDK_ROOT=$(NRF52_SDK_ROOT) SMALL=1

$(WOLFBOOT_BIN):
	cp target.h $(WOLFBOOT)/include
	cp nrf52.ld $(WOLFBOOT)/hal
	make -C $(WOLFBOOT) BOOT0_OFFSET=0x10000 VTOR=0 TARGET=nrf52 DEBUG=$(DEBUG) wolfboot.bin 

nrf5_iot_sdk_3288530.zip:
	wget https://developer.nordicsemi.com/nRF5_IoT_SDK/nRF5_IoT_SDK_v0.9.x/nrf5_iot_sdk_3288530.zip
	unzip nrf5_iot_sdk_3288530.zip -d ./nrf5x-softdevice

clean:
	make -C $(WOLFBOOT) clean
	make -C ota-server clean
	make -C $(DTLS_OTA) TARGET=nrf52dk NRF52_SDK_ROOT=$(NRF52_SDK_ROOT) clean
	rm -f $(DTLS_OTA)/*.bin 
	rm -f tags
	
$(BOOT_IMG)_v1_signed.bin: $(BOOT_ELF)
	python3 $(WOLFBOOT)/tools/keytools/sign.py $(BOOT_IMG).bin $(WOLFBOOT)/ed25519.der 1

$(DTLS_OTA)/dtls-ota-signed.bin: $(BOOT_IMG)_v1_signed.bin
	mv $^ $@

$(DTLS_OTA)/dtls-ota-force-update.bin: $(DTLS_OTA)/dtls-ota-signed.bin
	$(OBJCOPY) -I binary -O binary $^ tmp.bin --pad-to=0x27FFB --gap-fill=255
	cat tmp.bin force-update > $@
	rm tmp.bin

flash-update: $(DTLS_OTA)/dtls-ota-force-update.bin
	JLinkExe $(JLINK_OPTS) -CommanderScript flash_update.jlink 

flash: $(BOOT_ELF) $(WOLFBOOT_BIN) $(DTLS_OTA)/dtls-ota-signed.bin
	JLinkExe $(JLINK_OPTS) -CommanderScript flash_all.jlink 

erase:
	JLinkExe $(JLINK_OPTS) -CommanderScript flash_erase.jlink 

gdbserver:
	JLinkGDBServer -device nrf52 -if swd -port 3333
