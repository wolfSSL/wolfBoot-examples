ifdef WOLFBOOT_OFFSET

WOLFBOOT:=$(abspath $(RIOTBASE)/../../wolfBoot/)
CFLAGS += -I$(WOLFBOOT)/include

SIGNTOOL ?= python3 $(WOLFBOOT)/tools/keytools/sign.py
KEYGENTOOL ?= python3 $(WOLFBOOT)/tools/keytools/keygen.py

BINFILE ?= $(BINDIR)/$(APPLICATION).bin
SIGN_BINFILE = $(BINDIR)/$(APPLICATION)_v5_signed.bin
WOLFBOOT_KEYFILE ?= $(WOLFBOOT)/ecc256.der
WOLFBOOT_BIN ?= $(WOLFBOOT)/wolfboot.bin



export IMAGE_HDR_SIZE = 256

wolfboot-create-key: $(WOLFBOOT_KEYFILE)

$(WOLFBOOT_KEYFILE):
	make -C $(WOLFBOOT) clean
	make -C $(WOLFBOOT) distclean
	make -C $(WOLFBOOT) TARGET=nrf52 DEBUG=0 ecc256.der  \

wolfboot: wolfboot-create-key link
	@$(COLOR_ECHO)
	@$(COLOR_ECHO) '$(COLOR_PURPLE)Re-linking for wolfBoot at $(WOLFBOOT_OFFSET)...$(COLOR_RESET)'
	@$(COLOR_ECHO)
	@$(COLOR_ECHO) 'sources:'
	@$(COLOR_ECHO) $(SRC)
	@$(COLOR_ECHO)
	$(_LINK) $(LINKFLAGPREFIX)--defsym=_rom_start_addr="$$(($(WOLFBOOT_OFFSET) + $(IMAGE_HDR_SIZE)))" \
	$(LINKFLAGPREFIX)--defsym=length="$$(($(WOLFBOOT_PARTITION_SIZE) - $(IMAGE_HDR_SIZE)))" \
	$(LINKFLAGPREFIX)--defsym=image_header="$(IMAGE_HDR_SIZE)" -o $(ELFFILE) && \
	$(OBJCOPY) $(OFLAGS) -Obinary $(ELFFILE) $(BINFILE) && \
	$(SIGNTOOL) $(BINFILE) $(WOLFBOOT_KEYFILE) $(IMAGE_VERSION) $(WOLFBOOT_OFFSET)
	@$(COLOR_ECHO)
	@$(COLOR_ECHO) '$(COLOR_PURPLE)Signed with $(WOLFBOOT_KEYFILE) for version $(IMAGE_VERSION) \
		$(COLOR_RESET)'
	@$(COLOR_ECHO)


$(WOLFBOOT_BIN):
	@$(COLOR_ECHO) 'sources:'
	@$(COLOR_ECHO) $(SRC)
	@$(COLOR_ECHO)
	make -C $(WOLFBOOT) clean
	make -C $(WOLFBOOT) TARGET=nrf52 DEBUG=0 BOOT0_OFFSET=$(WOLFBOOT_OFFSET) \
		SIGN=ECC256 \
		WOLFBOOT_SECTOR_SIZE=0x1000 \
		WOLFBOOT_PARTITION_SIZE=0x40000 \
		WOLFBOOT_PARTITION_BOOT_ADDRESS=0x20000 \
		WOLFBOOT_PARTITION_UPDATE_ADDRESS=0x60000 \
		WOLFBOOT_PARTITION_SWAP_ADDRESS=0xE0000 \
		wolfboot.bin

.PHONY: wolfboot-flash-bootloader wolfboot-flash

wolfboot-flash-bootloader: HEXFILE = $(WOLFBOOT_BIN)
wolfboot-flash-bootloader: $(WOLFBOOT_BIN) $(FLASHDEPS)
	sudo $(FLASHER) $(FFLAGS) -o 0x0

wolfboot-flash: HEXFILE = $(SIGN_BINFILE)
wolfboot-flash: wolfboot $(FLASHDEPS) wolfboot-flash-bootloader
	sudo  $(FLASHER) $(FFLAGS) -o $(WOLFBOOT_OFFSET)

else
wolfboot:
	$(Q)echo "error: wolfboot not supported on board $(BOARD)!"
	$(Q)false
endif # WOLFBOOT_OFFSET
