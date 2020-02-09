# Copyright 2013-2019 René Ladan
# SPDX-License-Identifier: BSD-2-Clause

.PHONY: all clean install install-strip doxygen install-doxygen uninstall \
	uninstall-doxygen cppcheck iwyu

PREFIX?=.
ETCDIR?=etc/nplpi
CFLAGS+=-Wall -D_POSIX_C_SOURCE=200809L -DETCDIR=\"$(PREFIX)/$(ETCDIR)\" \
	-g -std=c99
INSTALL?=install
INSTALL_PROGRAM?=$(INSTALL)
CPPCHECK_ARGS?=--enable=all --inconclusive --language=c --std=c99 \
	-DETCDIR=\"$(ETCDIR)\"
# $(shell ...) does not work with FreeBSD make
JSON_C?=`pkg-config --cflags json-c`
JSON_L?=`pkg-config --libs json-c`

all: libnpl.so nplpi nplpi-analyze nplpi-readpin kevent-demo

hdrlib=input.h decode_time.h setclock.h mainloop.h calendar.h
srclib=${hdrlib:.h=.c}
objlib=${hdrlib:.h=.o}
objbin=nplpi.o nplpi-analyze.o nplpi-readpin.o kevent-demo.o

input.o: input.c input.h
	$(CC) -fpic $(CFLAGS) $(JSON_C) -c input.c -o $@
decode_time.o: decode_time.c decode_time.h calendar.h
	$(CC) -fpic $(CFLAGS) -c decode_time.c -o $@
setclock.o: setclock.c setclock.h decode_time.h input.h calendar.h
	$(CC) -fpic $(CFLAGS) -c setclock.c -o $@
mainloop.o: mainloop.c mainloop.h input.h decode_time.h setclock.h
	$(CC) -fpic $(CFLAGS) -c mainloop.c -o $@
calendar.o: calendar.c calendar.h
	$(CC) -fpic $(CFLAGS) -c calendar.c -o $@

libnpl.so: $(objlib)
	$(CC) -shared -o $@ $(objlib) -lm -lpthread $(JSON_L)

nplpi.o: decode_time.h input.h mainloop.h calendar.h nplpi.c
	$(CC) -fpic $(CFLAGS) $(JSON_C) -c nplpi.c -o $@
nplpi: nplpi.o libnpl.so
	$(CC) -o $@ nplpi.o -lncursesw libnpl.so -lpthread $(JSON_L)

nplpi-analyze.o: decode_time.h input.h mainloop.h calendar.h nplpi-analyze.c
nplpi-analyze: nplpi-analyze.o libnpl.so
	$(CC) -fpic $(CFLAGS) -c nplpi-analyze.c -o $@
	$(CC) -o $@ nplpi-analyze.o libnpl.so

nplpi-readpin.o: input.h nplpi-readpin.c
	$(CC) -fpic $(CFLAGS) $(JSON_C) -c nplpi-readpin.c -o $@
nplpi-readpin: nplpi-readpin.o libnpl.so
	$(CC) -o $@ nplpi-readpin.o libnpl.so $(JSON_L)

kevent-demo.o: input.h kevent-demo.c
	# __BSD_VISIBLE for FreeBSD < 12.0
	$(CC) -fpic $(CFLAGS) $(JSON_C) -c kevent-demo.c -o $@ -D__BSD_VISIBLE=1
kevent-demo: kevent-demo.o libnpl.so
	$(CC) -o $@ kevent-demo.o libnpl.so $(JSON_L)

doxygen:
	doxygen

clean:
	rm -f nplpi
	rm -f nplpi-analyze
	rm -f nplpi-readpin
	rm -f $(objbin)
	rm -f libnpl.so $(objlib)

install: libnpl.so nplpi nplpi-analyze nplpi-readpin
	mkdir -p $(DESTDIR)$(PREFIX)/lib
	$(INSTALL_PROGRAM) libnpl.so $(DESTDIR)$(PREFIX)/lib
	mkdir -p $(DESTDIR)$(PREFIX)/bin
	$(INSTALL_PROGRAM) nplpi nplpi-analyze nplpi-readpin \
		$(DESTDIR)$(PREFIX)/bin
	mkdir -p $(DESTDIR)$(PREFIX)/include/nplpi
	$(INSTALL) -m 0644 $(hdrlib) $(DESTDIR)$(PREFIX)/include/nplpi
	mkdir -p $(DESTDIR)$(PREFIX)/$(ETCDIR)
	$(INSTALL) -m 0644 etc/nplpi/config.json \
		$(DESTDIR)$(PREFIX)/$(ETCDIR)/config.json.sample
	mkdir -p $(DESTDIR)$(PREFIX)/share/doc/nplpi
	$(INSTALL) -m 0644 LICENSE \
		$(DESTDIR)$(PREFIX)/share/doc/nplpi

install-strip:
	$(MAKE) INSTALL_PROGRAM='$(INSTALL_PROGRAM) -s' install

install-doxygen: html
	mkdir -p $(DESTDIR)$(PREFIX)/share/doc/nplpi
	cp -R html $(DESTDIR)$(PREFIX)/share/doc/nplpi

install-md:
	mkdir -p $(DESTDIR)$(PREFIX)/share/doc/nplpi
	$(INSTALL) -m 0644 *.md \
		$(DESTDIR)$(PREFIX)/share/doc/nplpi

uninstall: uninstall-doxygen uninstall-md
	rm -f $(DESTDIR)$(PREFIX)/lib/libnpl.so
	rm -f $(DESTDIR)$(PREFIX)/bin/nplpi
	rm -f $(DESTDIR)$(PREFIX)/bin/nplpi-analyze
	rm -f $(DESTDIR)$(PREFIX)/bin/nplpi-readpin
	rm -rf $(DESTDIR)$(PREFIX)/include/nplpi
	rm -rf $(DESTDIR)$(PREFIX)/$(ETCDIR)
	rm -rf $(DESTDIR)$(PREFIX)/share/doc/nplpi

uninstall-doxygen:
	rm -rf $(DESTDIR)$(PREFIX)/share/doc/nplpi/html

uninstall-md:
	rm -f $(DESTDIR)$(PREFIX)/share/doc/nplpi/*.md

cppcheck:
	cppcheck -D__CYGWIN__ $(CPPCHECK_ARGS) . || true
	cppcheck -D__linux__ $(CPPCHECK_ARGS) . || true
	cppcheck -D__FreeBSD__ -D__FreeBSD_version=900022 \
		$(CPPCHECK_ARGS) . || true

iwyu:
	$(MAKE) clean
	$(MAKE) -k CC=include-what-you-use $(objlib) $(objbin) || true
