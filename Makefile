# Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.


daisydog:	daisydog.c
	$(CC) $(CFLAGS) $(CPPFLAGS) $(LDFLAGS) -o $@ $<

$(DESTDIR)/usr/sbin/daisydog:	daisydog
	$(INSTALL) --mode 555 -D -T $< $@

$(DESTDIR)/etc/init/daisydog.conf:	daisydog.conf
	$(INSTALL) --mode 644 -D -T $< $@

all:	daisydog

install:	$(DESTDIR)/etc/init/daisydog.conf \
		$(DESTDIR)/usr/sbin/daisydog

clean:
	rm -f daisydog *.o

.PHONY:	all install clean
