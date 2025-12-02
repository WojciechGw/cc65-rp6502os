CC65 ?= cl65

TARGET = hello.bin
SRCS = hello.c crt0_cmd.s write_stub.c
CFG = hello.cfg
MAP = hello.map

all: $(TARGET)

$(TARGET): $(SRCS) $(CFG)
	$(CC65) -t none -Oirs -C $(CFG) $(SRCS) -m $(MAP) -o $@

clean:
	rm -f $(TARGET) *.o *.lst

.PHONY: all clean
