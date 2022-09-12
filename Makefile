# Copyright 2011 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

top_srcdir ?= $(PWD)
vpath % $(top_srcdir)

CFLAGS += -Wall
CPPFLAGS += -D_FILE_OFFSET_BITS=64 -D_LARGEFILE_SOURCE -D_LARGEFILE64_SOURCE
CPPFLAGS += -I$(top_srcdir)

daisydog:	daisydog.c
	$(CC) $(CFLAGS) $(CPPFLAGS) $(LDFLAGS) -o $@ $<

$(DESTDIR)/usr/sbin/daisydog:	daisydog
	install --mode 555 -D -T $< $@

$(DESTDIR)/lib/udev/rules.d/51-watchdog.rules:	51-watchdog.rules
	install --mode 644 -D -T $< $@

$(DESTDIR)/etc/init/daisydog.conf:	daisydog.conf
	install --mode 644 -D -T $< $@

all:	daisydog

install:	$(DESTDIR)/lib/udev/rules.d/51-watchdog.rules \
		$(DESTDIR)/etc/init/daisydog.conf \
		$(DESTDIR)/usr/sbin/daisydog

clean:
	rm -f daisydog *.o

.PHONY:	all install clean
