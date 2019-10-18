# name of your application
APPLICATION = fw-update

WOLFBOOT_OFFSET=32768
WOLFBOOT_PARTITION_SIZE=110592
IMAGE_VERSION=5

# If no BOARD is found in the environment, use this default:
BOARD ?= samr21-xpro

# This has to be the absolute path to the RIOT base directory:
RIOTBASE ?= $(abspath $(CURDIR)/../RIOT/)

# Comment this out to disable code in RIOT that does safety checking
# which is not needed in a production environment but helps in the
# development process:
DEVELHELP ?= 1

# Change this to 0 show compiler invocation lines by default:
QUIET ?= 0

EXTERNAL_MODULE_DIRS += $(CURDIR)/libwolfboot
USEMODULE += libwolfboot

USEMODULE += gnrc_netdev_default
USEMODULE += auto_init_gnrc_netif
# Specify the mandatory networking modules for IPv6 and UDP
USEMODULE += gnrc_ipv6_default
USEMODULE += gnrc_sock_udp
USEMODULE += periph_flashpage
#USEMODULE += shell


WOLFBOOT_DIR=$(abspath $(RIOTBASE)/../../wolfBoot)
USEMODULE_INCLUDES+=-I$(WOLFBOOT_DIR)/include
CFLAGS+=-Wno-unused-parameter -Wno-unused-variable -Wno-missing-include-dirs

include $(RIOTBASE)/Makefile.include
include ../wolfboot.mk
