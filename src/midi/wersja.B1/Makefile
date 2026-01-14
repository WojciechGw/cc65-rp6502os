# Makefile dla MIDI-OUT Picocomputer 6502

CC = cc65
AS = ca65
LD = ld65

CFLAGS = -t none -O --standard c89
ASFLAGS = -t none
LDFLAGS = -C picocomputer.cfg

TARGET = midi_out.bin

OBJECTS = startup.o midi_out.o

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(LD) $(LDFLAGS) -o $@ $(OBJECTS)

midi_out.o: midi_out.c midi_out.h
	$(CC) $(CFLAGS) -o midi_out.s midi_out.c
	$(AS) $(ASFLAGS) -o $@ midi_out.s

startup.o: startup.s
	$(AS) $(ASFLAGS) -o $@ startup.s

clean:
	rm -f *.o *.s $(TARGET)

.PHONY: all clean
