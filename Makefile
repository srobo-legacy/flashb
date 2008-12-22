CFLAGS += -Wall -g

CFLAGS += `pkg-config $(PKG_CONFIG_ARGS) --cflags glib-2.0`
LDFLAGS += `pkg-config $(PKG_CONFIG_ARGS) --libs glib-2.0`

LDFLAGS += -lelf

flashb: flashb.c elf-access.c i2c.c smbus_pec.c msp430-fw.c i2c-blk.c \
	sr-i2c.c
	$(CC) -o flashb   $^ -lelf ${LDFLAGS} ${CFLAGS}

elf-access.c: elf-access.h
i2c.c: i2c.h
smbus_pec.c: smbus_pec.h
msp430-fw.c: msp430-fw.h

.PHONY: clean

clean:
	-rm -f flashb
