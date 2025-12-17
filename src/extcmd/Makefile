CC65 ?= cl65
CMD ?= EXTCMD
START ?= C000

TARGET = $(CMD)
SRCS = ../ext-$(TARGET).c crt0_cmd.s write_stub.c initmainargs.s
CFG = ../ext0x$(START).cfg
MAP = ext-$(TARGET).map

all: $(TARGET)

$(TARGET): $(SRCS) $(CFG)
	$(CC65) -t rp6502 -Oirs -C $(CFG) $(SRCS) -m $(MAP) -o build/$@.com
