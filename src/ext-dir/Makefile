CC65 ?= cl65

TARGET = dir
SRCS = $(TARGET).c crt0_cmd.s write_stub.c initmainargs.s
CFG = $(TARGET).cfg
MAP = $(TARGET).map

all: $(TARGET)

$(TARGET): $(SRCS) $(CFG)
	$(CC65) -t rp6502 -Oirs -C $(CFG) $(SRCS) -m $(MAP) -o $@.com

clean:
	rm -f $(TARGET).com *.o *.lst

.PHONY: all clean
