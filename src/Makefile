CC65 ?= cl65

SRCS = ted.c extcmd/crt0_cmd.s extcmd/write_stub.c extcmd/initmainargs.s
CFG  = ted.cfg
MAP  = extcmd/map/ted.map

all: ted

ted: $(SRCS) $(CFG)
	$(CC65) -t rp6502 -Oirs -C $(CFG) $(SRCS) -m $(MAP) -o extcmd/build/ted.com
