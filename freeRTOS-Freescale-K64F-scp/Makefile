CROSS_COMPILE:=arm-none-eabi-
CC:=$(CROSS_COMPILE)gcc
LD:=$(CROSS_COMPILE)gcc
AS:=$(CROSS_COMPILE)gcc
OBJCOPY:=$(CROSS_COMPILE)objcopy
SIZE:=$(CROSS_COMPILE)size
KINETIS?=$(HOME)/src/FRDM-K64F
KINETIS_DRIVERS?=$(KINETIS)/devices/MK64F12
KINETIS_CMSIS?=$(KINETIS)/CMSIS
PHY=$(KINETIS)/components/phyksz8081
FREERTOS_PORT:=freeRTOS/portable/GCC/ARM_CM4F
WOLFBOOT:=../wolfBoot
WOLFSSL_ROOT:=../wolfBoot/lib/wolfssl
WOLFSSH_ROOT:=$(PWD)/wolfssh
WOLFSSL_BUILD:=./build/lib
DEBUG?=0

CFLAGS=-mcpu=cortex-m4 -mfloat-abi=hard -mfpu=fpv4-sp-d16 \
	   -DFREERTOS \
	   -DNVM_FLASH_WRITEONCE \
       -Wno-address-of-packed-member
CFLAGS+=-mthumb -Wall -Wextra -Wno-main -Wstack-usage=1024 -ffreestanding -Wno-unused \
		-Isrc \
	-Ilib/bootutil/include -Iinclude/ -Ilib/wolfssl -I$(FREERTOS_PORT) -nostartfiles \
	-IfreeRTOS -IfreeRTOS/include -I build/include -I$(WOLFBOOT)/include -I$(WOLFBOOT) \
	-DWOLFSSL_USER_SETTINGS -I$(WOLFSSL_ROOT) -I$(WOLFSSH_ROOT) -DPICO_PORT_CUSTOM \
	-mthumb -mlittle-endian -mthumb-interwork -ffreestanding -fno-exceptions \
	-DWOLFBOOT_HASH_SHA256

ifneq ($(DEBUG),0)
  CFLAGS+=-O0 -ggdb3
else
  CFLAGS+=-Os
endif


CFLAGS+=-I$(KINETIS_DRIVERS)/drivers -I$(KINETIS_DRIVERS) -DCPU_MK64FN1M0VLL12 -I$(KINETIS_CMSIS)/Include -I$(PHY) -DDEBUG_CONSOLE_ASSERT_DISABLE=1
LDFLAGS=$(CFLAGS) -Wl,-gc-sections -ffreestanding -nostartfiles -lc -lnosys -Wl,-Map=image.map -specs=nano.specs




OBJS:= \
  $(KINETIS_DRIVERS)/drivers/fsl_clock.o \
  $(KINETIS_DRIVERS)/drivers/fsl_ftfx_flash.o \
  $(KINETIS_DRIVERS)/drivers/fsl_ftfx_cache.o \
  $(KINETIS_DRIVERS)/drivers/fsl_ftfx_controller.o \
  $(KINETIS_DRIVERS)/drivers/fsl_enet.o \
  $(KINETIS_DRIVERS)/drivers/fsl_sysmpu.o \
  $(WOLFBOOT)/src/libwolfboot.o \
  $(WOLFBOOT)/hal/kinetis.o \
  $(WOLFSSH_ROOT)/src/internal.o \
  $(WOLFSSH_ROOT)/src/ssh.o \
  $(WOLFSSH_ROOT)/src/log.o \
  $(WOLFSSH_ROOT)/src/io.o \
  $(WOLFSSH_ROOT)/src/port.o \
  $(WOLFSSH_ROOT)/src/wolfscp.o \
  $(WOLFSSL_BUILD)/wolfcrypt/signature.o \
  src/clock_config.o \
  src/main.o \
  src/pin_mux.o \
  freeRTOS/croutine.o \
  freeRTOS/event_groups.o \
  freeRTOS/list.o \
  freeRTOS/queue.o \
  freeRTOS/stream_buffer.o \
  freeRTOS/tasks.o \
  freeRTOS/timers.o \
  freeRTOS/printf-stdarg.o \
  $(FREERTOS_PORT)/port.o \
  freeRTOS/portable/MemMang/heap_5.o \
  src/startup_mk64f12.o \
  src/pico_enet_kinetis.o \
  $(PHY)/fsl_phy.o \
  src/picotcp.o

WOLFSSL_OBJS += 	\
    $(WOLFSSL_BUILD)/internal.o \
	$(WOLFSSL_BUILD)/wolfio.o \
    $(WOLFSSL_BUILD)/keys.o \
    $(WOLFSSL_BUILD)/crl.o \
    $(WOLFSSL_BUILD)/ssl.o \
    $(WOLFSSL_BUILD)/tls.o \
	$(WOLFSSL_BUILD)/wolfcrypt/aes.o  \
	$(WOLFSSL_BUILD)/wolfcrypt/asn.o  \
	$(WOLFSSL_BUILD)/wolfcrypt/chacha.o  \
	$(WOLFSSL_BUILD)/wolfcrypt/chacha20_poly1305.o  \
	$(WOLFSSL_BUILD)/wolfcrypt/coding.o  \
	$(WOLFSSL_BUILD)/wolfcrypt/curve25519.o  \
	$(WOLFSSL_BUILD)/wolfcrypt/error.o  \
	$(WOLFSSL_BUILD)/wolfcrypt/ecc.o  \
	$(WOLFSSL_BUILD)/wolfcrypt/ed25519.o  \
	$(WOLFSSL_BUILD)/wolfcrypt/rsa.o  \
	$(WOLFSSL_BUILD)/wolfcrypt/fe_low_mem.o  \
	$(WOLFSSL_BUILD)/wolfcrypt/fe_operations.o  \
	$(WOLFSSL_BUILD)/wolfcrypt/ge_low_mem.o  \
	$(WOLFSSL_BUILD)/wolfcrypt/ge_operations.o  \
	$(WOLFSSL_BUILD)/wolfcrypt/hash.o  \
	$(WOLFSSL_BUILD)/wolfcrypt/hmac.o  \
	$(WOLFSSL_BUILD)/wolfcrypt/integer.o  \
	$(WOLFSSL_BUILD)/wolfcrypt/logging.o  \
	$(WOLFSSL_BUILD)/wolfcrypt/md5.o  \
	$(WOLFSSL_BUILD)/wolfcrypt/memory.o  \
	$(WOLFSSL_BUILD)/wolfcrypt/poly1305.o  \
	$(WOLFSSL_BUILD)/wolfcrypt/pwdbased.o  \
	$(WOLFSSL_BUILD)/wolfcrypt/random.o  \
	$(WOLFSSL_BUILD)/wolfcrypt/sha.o  \
	$(WOLFSSL_BUILD)/wolfcrypt/sha256.o  \
	$(WOLFSSL_BUILD)/wolfcrypt/sha512.o  \
	$(WOLFSSL_BUILD)/wolfcrypt/wc_encrypt.o  \
	$(WOLFSSL_BUILD)/wolfcrypt/wc_port.o  \
	$(WOLFSSL_BUILD)/wolfcrypt/wolfmath.o
	
OBJS_SPMATH:= $(WOLFSSL_BUILD)/wolfcrypt/sp_c32.o  \
 		 		$(WOLFSSL_BUILD)/wolfcrypt/sp_int.o

OBJS+=$(WOLFSSL_OBJS) $(OBJS_SPMATH)
LIBS+=build/lib/libpicotcp.a

vpath %.c $(dir $(WOLFSSL_ROOT)/src)
vpath %.c $(dir $(WOLFSSL_ROOT)/wolfcrypt/src)

wolfboot:LSCRIPT:=k64f_wolfboot.ld
wolfboot: image.bin wolfboot-align.bin
	python3 ../wolfBoot/tools/keytools/sign.py --ecc256 image.bin ../wolfBoot/ecc256.der 1
	cat wolfboot-align.bin image_v1_signed.bin >factory.bin
	sleep 1
	touch image.bin
	python3 ../wolfBoot/tools/keytools/sign.py --ecc256 image.bin ../wolfBoot/ecc256.der 2

wolfboot-align.bin:CFLAGS=-mthumb -Wall -Wextra -Wno-main -Wstack-usage=1024 -ffreestanding -Wno-unused \
		-Isrc \
	-Ilib/bootutil/include -Iinclude/ -Ilib/wolfssl -I$(FREERTOS_PORT) -nostartfiles \
	-IfreeRTOS -IfreeRTOS/include -I build/include -I$(WOLFBOOT)/include -I$(WOLFBOOT) \
	-DWOLFSSL_USER_SETTINGS -I$(WOLFSSL_ROOT)  -DPICO_PORT_CUSTOM \
	-mthumb -mlittle-endian -mthumb-interwork -ffreestanding -fno-exceptions
wolfboot-align.bin:CFLAGS+=-I$(KINETIS_DRIVERS)/drivers -I$(KINETIS_DRIVERS) -DCPU_MK64FN1M0VLL12 -I$(KINETIS_CMSIS)/Include -I$(PHY) -DDEBUG_CONSOLE_ASSERT_DISABLE=1 -mcpu=cortex-m3 -DNVM_FLASH_WRITEONCE=1
wolfboot-align.bin:LDFLAGS=$(CFLAGS) -Wl,-gc-sections -ffreestanding -nostartfiles -lc -lnosys -Wl,-Map=image.map -specs=nano.specs
wolfboot-align.bin: wolfboot_target
	rm -f ../wolfBoot/hal/kinetis.o
	rm -f ../wolfBoot/src/*.o
	rm -f $(KINETIS_DRIVERS)/drivers/*.o
	cp src/wolfboot.config ../wolfBoot/.config
	make -C ../wolfBoot wolfboot-align.bin
	cp ../wolfBoot/wolfboot-align.bin .

standalone:LSCRIPT:=k64f_standalone.ld
standalone: image.bin
	
$(LIBS): picotcp
	make -C picotcp EXTRA_CFLAGS="-DPICO_PORT_CUSTOM $(CFLAGS) -I../src -I../freeRTOS/include -I../freeRTOS -I../$(FREERTOS_PORT)" \
		ARCH=cortexm4-hardfloat CROSS_COMPILE=arm-none-eabi- RTOS=1 \
		AODV=0 LOOP=0 PPP=0 DHCP_SERVER=0 DNS_SD=0 FRAG=0 ICMP6=0 \
		IPV6=0 NAT=0 MDNS=0 MCAST=0 TFTP=0 SNTP=0 SLAACV4=0 MD5=0 \
		DEBUG=0

$(WOLFSSL_BUILD)/wolfcrypt:
	mkdir -p $(@)

%.o:%.S
	$(CC) -c -o $(@) $(CFLAGS) $^

%.o:%.c
	$(CC) -c -o $(@) $(CFLAGS) $^

$(WOLFSSL_BUILD)/%.o: $(WOLFSSL_ROOT)/src/%.c
	$(CC) -c -o $(@) $(CFLAGS) $^

$(WOLFSSL_BUILD)/wolfcrypt/%.o: $(WOLFSSL_ROOT)/wolfcrypt/src/%.c
	$(CC) -c -o $(@) $(CFLAGS) $^

image.bin: image.elf wolfboot_target
	$(OBJCOPY) -O binary image.elf $@
	$(SIZE) image.elf

image.hex: image.elf
	$(OBJCOPY) -O ihex $^ $@

wolfboot_target: FORCE
	cp -f src/wolfboot.config ../wolfBoot/.config
	make -C ../wolfBoot include/target.h

image.elf: wolfboot_target $(WOLFSSL_BUILD)/wolfcrypt $(LIBS) $(OBJS) $(LSCRIPT)
	$(LD) $(LDFLAGS) -Wl,--start-group $(OBJS) $(LIBS) -Wl,--end-group -o $@ -T $(LSCRIPT)

clean:
	rm -f *.bin *.elf $(OBJS) wolfboot.map *.bin  *.hex src/*.o freeRTOS/*.o $(FREERTOS_PORT)/*.o *.map tags
	make -C picotcp clean

FORCE:
