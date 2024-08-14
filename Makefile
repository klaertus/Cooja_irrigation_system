CONTIKI_PROJECT = custom-routing
all: $(CONTIKI_PROJECT)

TARGET_ARCH = z1

MAKE_MAC ?= MAKE_MAC_CSMA
MAKE_NET = MAKE_NET_NULLNET

PROJECT_SOURCEFILES += protocol.c

CONTIKI = /home/user/contiki-ng/
include $(CONTIKI)/Makefile.include

