CROSS_COMPILE:=arm-none-eabi-
OBJCOPY:=$(CROSS_COMPILE)objcopy
JLINK_OPTS = -Device NRF52840_xxAA -if swd -speed 1000


APP_SRC:=./nrf52-gatt-service
BOOT_ELF:=nrf52-gatt-service/bin/nrf52840dk/nrf52-gatt-service.elf
BOOT_IMG:=nrf52-gatt-service/bin/nrf52840dk/nrf52-gatt-service_v1_signed.bin
UPDATE_IMG:=./nrf52-gatt-service/bin/nrf52840dk/nrf52-gatt-service_v2_signed.bin
WOLFBOOT=../wolfBoot
WOLFBOOT_BIN=$(WOLFBOOT)/wolfboot.bin


all: $(WOLFBOOT_BIN)

$(BOOT_IMG):
	@cp -f ./wolfboot-config $(WOLFBOOT)/.config
	@make -C $(WOLFBOOT) clean
	make wolfboot -C $(APP_SRC) IMAGE_VERSION=1


$(WOLFBOOT_BIN): $(BOOT_IMG)
	@make -C $(WOLFBOOT) wolfboot.bin

clean: FORCE
	make -C $(WOLFBOOT) clean
	make -C $(APP_SRC) clean
	rm -f $(APP_SRC)/*.bin 
	rm -f *.bin
	rm -f tags
	rm -rf linux-bluez/__pycache__

distclean: clean
	rm -rf $(APP_SRC)/bin

	
flash: $(WOLFBOOT_BIN) $(BOOT_IMG)
	JLinkExe $(JLINK_OPTS) -CommanderScript flash_all.jlink 

reset: FORCE
	JLinkExe $(JLINK_OPTS) -CommanderScript reset.jlink

update: FORCE
	@make wolfboot -C $(APP_SRC) IMAGE_VERSION=2
	@echo UPDATE ready. Please execute
	@echo linux-bluez/fwupdate.py $(UPDATE_IMG) 
	@echo as root.


erase: FORCE
	JLinkExe $(JLINK_OPTS) -CommanderScript flash_erase.jlink 


.PHONY: FORCE
