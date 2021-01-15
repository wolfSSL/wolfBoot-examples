# name of your application
APPLICATION = nrf52-gatt-service

#debug
DEBUG=0

# If no BOARD is found in the environment, use this default:
BOARD ?= nrf52840dk
#BOARD ?= arduino-nano-33-iot

WOLFBOOT_OFFSET=131072
WOLFBOOT_PARTITION_SIZE=262144
IMAGE_VERSION=514

# This has to be the absolute path to the RIOT base directory:
RIOTBASE ?= $(CURDIR)/../RIOT/

# Require uart module
FEATURES_REQUIRED += periph_uart

EXTERNAL_MODULE_DIRS += $(CURDIR)/libwolfboot
USEMODULE += libwolfboot

# Some RIOT modules needed for this example
USEMODULE += event_timeout

# Network
USEMODULE += nimble
USEMODULE += nimble_addr
USEMODULE += nimble_scanner
USEMODULE += nimble_scanlist
USEMODULE += nimble_svc_gap
USEMODULE += nimble_svc_gatt
USEMODULE += nimble_drivers_nrf5x
USEMODULE += random

USEMODULE += periph_flashpage



#USEMODULE += usbus
#USEMODULE += stdio_cdc_acm
#USEMODULE += auto_init_usbus
USEMODULE += shell
USEMODULE += shell_commands
USEMODULE += ps

# Comment this out to disable code in RIOT that does safety checking
# which is not needed in a production environment but helps in the
# development process:
DEVELHELP ?= 0
CFLAGS+=-Wno-missing-field-initializers -Wno-unused-parameter -Wno-unused-variable -Wno-unused-function -I$(CURDIR)/include -Wno-missing-include-dirs

CFLAGS+=-DWOLFBOOT_HASH_SHA256 -DWOLFBOOT_SIGN_ECC256
WOLFBOOT_DIR=$(abspath $(RIOTBASE)/../../wolfBoot)
USEMODULE_INCLUDES+=-I$(WOLFBOOT_DIR)/include

# Change this to 0 show compiler invocation lines by default:
QUIET ?= 1

include $(RIOTBASE)/Makefile.include
include nimble.inc.mk
include ../wolfboot.mk
