CFLAGS += -Wall -g

CFLAGS += `pkg-config $(PKG_CONFIG_ARGS) --cflags glib-2.0 libsric`
LDFLAGS += `pkg-config $(PKG_CONFIG_ARGS) --libs glib-2.0 libsric`
LDFLAGS += -lelf


flashb: flashb.c elf-access.c msp430-fw.c
	$(CC) $(CFLAGS) $(LDFLAGS) -o flashb $^

install: flashb
	install -d $(DESTDIR)$(PREFIX)/bin
	install flashb $(DESTDIR)$(PREFIX)/bin/flashb

elf-access.c: elf-access.h
smbus_pec.c: smbus_pec.h
msp430-fw.c: msp430-fw.h

.PHONY: clean

clean:
	-rm -f flashb
