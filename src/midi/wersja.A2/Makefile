# ============================================================
# Makefile dla MIDI-OUT Picocomputer 6502
# System: 64kB RAM, 8 MHz, VIA @ $FFD0
# ============================================================

CA65    = ca65
CC65    = cc65
LD65    = ld65

# Opcje kompilacji
CAFLAGS = --cpu 65C02
CCFLAGS = -t none -O --cpu 65C02
LDFLAGS = -C picocomputer.cfg

# Pliki
TARGET  = midi.bin
OBJECTS = crt0.o midi_irq.o midi_out.o

# Mapa pamięci (opcjonalnie)
MAPFILE = midi.map

# ============================================================
# Reguły
# ============================================================

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(LD65) $(LDFLAGS) -m $(MAPFILE) -o $@ $(OBJECTS)
	@echo "=== Build complete: $(TARGET) ==="
	@echo "=== Load address: 0x0200 ==="

crt0.o: crt0.s
	$(CA65) $(CAFLAGS) -o $@ $<

midi_irq.o: midi_irq.s
	$(CA65) $(CAFLAGS) -o $@ $<

midi_out.o: midi_out.c
	$(CC65) $(CCFLAGS) -o midi_out.s $<
	$(CA65) $(CAFLAGS) -o $@ midi_out.s

# Wyczyść pliki tymczasowe
clean:
	rm -f *.o midi_out.s $(TARGET) $(MAPFILE)

# Informacje o rozmiarze
size: $(TARGET)
	@echo "=== Binary size ==="
	@ls -la $(TARGET)
	@echo "=== Section sizes (from map) ==="
	@grep -E "^(CODE|DATA|BSS|RODATA|ZEROPAGE)" $(MAPFILE) || true

.PHONY: all clean size
