# Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.


daisydog:	daisydog.c
	$(CC) -o $< $(CFLAGS) $@

$(DESTDIR)/usr/sbin/daisydog:	daisydog
	$(ECHO) "Installing '$<' to '$@'"
	$(INSTALL) --mode 555 -D $< $@

$(DESTDIR)/etc/init/daisydog.conf:	daisydog.conf
	$(ECHO) "Installing '$<' to '$@'"
	$(INSTALL) --mode 644 -D $< $@


all:	daisydog

install:	$(DESTDIR)/etc/init/daisydog.conf \
		$(DESTDIR)/usr/sbin/daisydog

clean:

.PHONY:	clean daisydog_install
