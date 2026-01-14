# Makefile dla MIDI-OUT RP6502

TARGET = midi_out
SOURCES_C = midi_out.c
SOURCES_S = startup.s

CC = cl65
CPU = 65C02
SYSTEM = rp6502

CFLAGS = --cpu $(CPU) -t $(SYSTEM) -O

.PHONY: all clean

all: $(TARGET).rp6502

$(TARGET).rp6502: $(SOURCES_C) $(SOURCES_S) midi_out.h
	$(CC) $(CFLAGS) -o $@ $(SOURCES_S) $(SOURCES_C)

clean:
	rm -f $(TARGET).rp6502 *.o
