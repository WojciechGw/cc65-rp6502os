# Kompiluj startup
ca65 crt0.s -o crt0.o

# Kompiluj program główny
cc65 -t none -O midi_out.c -o midi_out.s
ca65 midi_out.s -o midi_out.o

# Linkuj
ld65 -C picocomputer.cfg crt0.o midi_out.o -o midi_out.bin

# Lub jednym poleceniem (używając domyślnego crt0):
cl65 -t none -C picocomputer.cfg midi_out.c -o midi_out.bin
