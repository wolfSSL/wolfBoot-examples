# Inherit our settings for wolfBoot, TARGET, ARCH, etc.
-include measured.wolfboot.config

# Make sure environment variables do not corrupt the binary output for MacOS users
LANG=
LC_COLLATE="C"
LC_CTYPE="C"
LC_MESSAGES="C"
LC_MONETARY="C"
LC_NUMERIC="C"
LC_TIME="C"
LC_ALL=

APPSRC:=./src
WOLFBOOT:=../wolfBoot
WOLFSSL_ROOT:=../wolfBoot/lib/wolfssl
WOLFTPM_ROOT:=../wolfBoot/lib/wolfTPM
ECCKEY:=$(WOLFBOOT)/ecc256.der
DEBUG?=1

include $(WOLFBOOT)/tools/config.mk

CFLAGS:=-g -ggdb -Wall -Wstack-usage=1024 -ffreestanding -Wno-unused -DPLATFORM_$(TARGET) \
        -I$(WOLFBOOT)/include -I$(WOLFBOOT) -I$(WOLFSSL_ROOT) -I$(WOLFTPM_ROOT) \
        -DWOLFBOOT_MEASURED_PCR_A -nostartfiles
CFLAGS+=-DWOLFBOOT_HASH_SHA256
CFLAGS+=-DWOLFSSL_USER_SETTINGS
CFLAGS+=-DWOLFTPM_USER_SETTINGS

APP_OBJS:= \
	$(APPSRC)/app_$(TARGET).o \
	$(APPSRC)/led.o \
	$(APPSRC)/system.o \
	$(APPSRC)/timer.o \
	$(WOLFBOOT)/hal/$(TARGET).o \
	$(WOLFBOOT)/src/libwolfboot.o \
	$(WOLFBOOT)/hal/spi/spi_drv_stm32.o \
	$(APPSRC)/startup_arm.o

# Add objects for wolfCrypt support required by wolfTPM
APP_OBJS+= \
	$(WOLFSSL_ROOT)/wolfcrypt/src/hmac.o \
	$(WOLFSSL_ROOT)/wolfcrypt/src/aes.o \
	$(WOLFSSL_ROOT)/wolfcrypt/src/wc_port.o

# Add objects for wolfTPM support
APP_OBJS+= \
	$(WOLFTPM_ROOT)/src/tpm2.o \
	$(WOLFTPM_ROOT)/src/tpm2_packet.o \
	$(WOLFTPM_ROOT)/src/tpm2_tis.o \
	$(WOLFTPM_ROOT)/src/tpm2_wrap.o \
	$(WOLFTPM_ROOT)/src/tpm2_param_enc.o

# Inherit cross-compiler and similar settings from wolfBoot
include ../wolfBoot/arch.mk

ifneq ($(DEBUG),0)
	CFLAGS+=-O0 -ggdb3
else
	CFLAGS+=-Os
endif

vpath %.c $(dir $(WOLFSSL_ROOT)/src)
vpath %.c $(dir $(WOLFSSL_ROOT)/wolfcrypt/src)
vpath %.c $(dir $(WOLFBOOT))/lib/wolfTPM/wolftpm)

ENTRY_POINT=`cat .entry-point-address`
LSCRIPT:=$(APPSRC)/target-app.ld
LSCRIPT_TEMPLATE:=$(APPSRC)/$(ARCH).ld
LDFLAGS:=$(CFLAGS) -T $(LSCRIPT) -Wl,-gc-sections -Wl,-Map=image.map


wolfboot-example: wolfboot_align.bin image.bin
	python3 $(WOLFBOOT)/tools/keytools/sign.py --ecc256 image.bin $(ECCKEY) 1
	cat wolfboot-align.bin image_v1_signed.bin >factory.bin

wolfboot-align.bin:LSCRIPT:=$(WOLFBOOT)/target.ld
wolfboot_align.bin:CFLAGS+=-DWOLFBOOT_HASH_SHA256
wolfboot_align.bin: wolfboot_target
	make -C $(WOLFBOOT) align
	cp $(WOLFBOOT)/wolfboot-align.bin .
	cp $(WOLFBOOT)/wolfboot.elf .

image.bin: wolfboot_target image.elf
	$(OBJCOPY) -O binary image.elf $@
	$(SIZE) image.elf

image.elf: wolfboot_target $(APP_OBJS) $(LSCRIPT)
	@echo "\t[LD] $@"
	$(Q)$(LD) $(LDFLAGS) $(APP_OBJS) -o $@

wolfboot_target: FORCE
	cp -f measured.wolfboot.config $(WOLFBOOT)/.config
	make -C $(WOLFBOOT) include/target.h

%.o:%.c
	@echo "\t[CC-$(ARCH)] $@"
	$(Q)$(CC) $(CFLAGS) -c -o $@ $^

%.o:%.S
	@echo "\t[AS-$(ARCH)] $@"
	$(Q)$(CC) $(CFLAGS) -c -o $@ $^

clean:
	make -C $(WOLFBOOT) clean
	@rm -f *.bin *.elf $(OBJS) wolfboot.map *.bin  *.hex src/*.o tags *.map

$(LSCRIPT): $(LSCRIPT_TEMPLATE) FORCE
	@printf "%d" $(WOLFBOOT_PARTITION_BOOT_ADDRESS) > .wolfboot-offset
	@printf "%d" $(WOLFBOOT_PARTITION_SIZE) > .partition-size
	@printf "%d" $(IMAGE_HEADER_SIZE) > .header-size
	@expr `cat .wolfboot-offset` + `cat .header-size` > .entry-point
	@printf "0x%X" `cat .entry-point` > .entry-point
	@expr `cat .partition-size` - `cat .header-size` > .app-size
	@printf "0x%X" `cat .app-size` > .app-size
	@ cat $(LSCRIPT_TEMPLATE) | \
		sed -e "s/##WOLFBOOT_TEST_APP_SIZE##/`cat .app-size`/g" | \
		sed -e "s/##WOLFBOOT_TEST_APP_ADDRESS##/`cat .entry-point`/g" \
		> $(@)
	@rm -f .app-size .entry-point .wolfboot-offset .partition-size .header-size

flash: FORCE
	st-flash write factory.bin 0x08000000

erase: FORCE
	st-flash erase

FORCE:

.PHONY: FORCE clean
