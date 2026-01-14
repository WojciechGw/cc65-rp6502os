# Makefile dla MIDI-OUT Picocomputer 6502
# 64KB RAM, VIA @ $FFD0, 8 MHz

CC = cc65
AS = ca65
LD = ld65

# Opcje kompilatora C
CFLAGS = -t none -O --cpu 65C02 --standard c89

# Opcje asemblera
ASFLAGS = -t none --cpu W65C02

# Opcje linkera
LDFLAGS = -C picocomputer.cfg

# Plik wynikowy
TARGET = midi_out.bin

# Pliki obiektowe
OBJECTS = startup.o midi_out.o

# Regula domyslna
all: $(TARGET)

# Linkowanie
$(TARGET): $(OBJECTS)
	$(LD) $(LDFLAGS) -o $@ $(OBJECTS) -m midi_out.map

# Kompilacja C -> ASM -> OBJ
midi_out.o: midi_out.c midi_out.h
	$(CC) $(CFLAGS) -o midi_out.s midi_out.c
	$(AS) $(ASFLAGS) -o $@ midi_out.s

# Asemblacja
startup.o: startup.s
	$(AS) $(ASFLAGS) -o $@ startup.s

# Czyszczenie
clean:
	rm -f *.o *.s $(TARGET) *.map

# Ladowanie do Picocomputer (przyklad)
upload: $(TARGET)
	@echo "Ladowanie $(TARGET) do Picocomputer..."
	# Tutaj komenda do ladowania, np:
	# python3 upload.py $(TARGET)

.PHONY: all clean upload
