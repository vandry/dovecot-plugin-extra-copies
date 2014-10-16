PREFIX=$(DESTDIR)/usr/lib
DOVECOT_INC=/usr/include/dovecot
DOVECOT_CFLAGS=-I$(DOVECOT_INC) -DHAVE_CONFIG_H
CFLAGS=-fPIC -shared -Wall -I$(DOVECOT_INC) -DHAVE_CONFIG_H

INSTALL_DEST=$(PREFIX)/dovecot/modules

OBJ=extra-copies.o

all: extra_copies_plugin.so

extra_copies_plugin.so: $(OBJ)
	$(CC) -shared -o $@ $(OBJ)

clean:
	rm -rf $(OBJ) extra_copies_plugin extra_copies_plugin.so

install: extra_copies_plugin.so
	mkdir -p $(INSTALL_DEST)
	install -g bin -o root -m 0644 extra_copies_plugin.so $(INSTALL_DEST)
