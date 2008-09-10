CFLAGS += -Wall

CFLAGS += `pkg-config --cflags glib-2.0`
LDFLAGS += `pkg-config --libs glib-2.0`

LDFLAGS += -lelf

flashb: flashb.c elf-access.c i2c.c

elf-access.c: elf-access.h

i2c.c: i2c.h

.PHONY: clean

clean:
	-rm -f flashb
