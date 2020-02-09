# Copyright 2017,2019 René Ladan
# SPDX-License-Identifier: BSD-2-Clause

.PHONY: all clean test

objbin=test_calendar.o
exebin=${objbin:.o=}

all: test
test: $(exebin)
	./test_calendar

JSON_L?=`pkg-config --libs json-c`
PREFIX?=.
ETCDIR?=etc/nplpi
CFLAGS+=-Wall -D_POSIX_C_SOURCE=200809L -DETCDIR=\"$(PREFIX)/$(ETCDIR)\" \
	-g -std=c99

test_calendar.o: ../calendar.h
	$(CC) -fpic $(CFLAGS) -I.. -c test_calendar.c -o $@
test_calendar: test_calendar.o ../calendar.o
	$(CC) -o $@ test_calendar.o ../calendar.o

clean:
	rm -f $(objbin) $(exebin)
