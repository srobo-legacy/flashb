CFLAGS += -Wall -g

PKG_CONFIG = `pkg-config $(PKG_CONFIG_ARGS) --cflags glib-2.0`
LDFLAGS += `pkg-config $(PKG_CONFIG_ARGS) --libs glib-2.0`

LDFLAGS += -lelf -lsric

flashb: flashb.c elf-access.c msp430-fw.c
	$(CC) -o flashb   $^ -lelf ${LDFLAGS} ${PKG_CONFIG} ${CFLAGS}

elf-access.c: elf-access.h
smbus_pec.c: smbus_pec.h
msp430-fw.c: msp430-fw.h

.PHONY: clean

clean:
	-rm -f flashb
