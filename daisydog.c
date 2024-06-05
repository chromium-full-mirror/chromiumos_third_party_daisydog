/*
 * Simple Linux HW watchdog daemon
 *
 * Copyright (c) 2010 Daniel Widyanko. All rights reserved.
 * Copyright 2012 The ChromiumOS Authors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */
#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <getopt.h>
#include <malloc.h>
#include <string.h>
#include <signal.h>
#include <errno.h>
#include <unistd.h>

#include <linux/watchdog.h>

#define WATCHDOGDEV "/dev/watchdog"
#define MIN_WD_TIMEOUT 5	/* WD must be at least 5 seconds */
#define DEFAULT_INTERVAL_SECS 30

#define PETTING_SECS 2		/* Try to pet every 2 seconds */

/* "volatile" prevents gcc from optimizing accesses to terminated. */
static volatile sig_atomic_t terminated;
static const char short_options[] = "hcd:i:";
static const struct option long_options[] = {
	{"help", 0, NULL, 'h'},
	{"check", 0, NULL, 'c'},
	{"dev", 1, NULL, 'd'},
	{"interval", 1, NULL, 'i'},
	{NULL, 0, NULL, 0},
};

static void print_usage(FILE * stream, char *app_name, int exit_code)
{
	fprintf(stream, "Usage: %s [options]\n", app_name);
	fprintf(stream,
" -h  --help                Display this usage information.\n"
" -c  --check               Exit right away after printing info.\n"
" -d  --dev <device_file>   Use <device_file> as HW watchdog device file.\n"
"                           The default is '/dev/watchdog'\n"
" -i  --interval <interval> Change the HW watchdog interval time\n"
"                           Must be at least %d seconds\n", MIN_WD_TIMEOUT
	);

	exit(exit_code);
}

static void daisydog_sigterm(int signal)
{
	terminated = 1;
}

/*
 * Writing 'V' into watchdog device indicates the close/stop
 * of the watchdog was intentional. Otherwise, debug message
 * 'Watchdog timer closed unexpectedly' will be printed to
 * dmesg and the system will reboot in wd_timeout seconds since
 * the last time the watchdog was pet.
 */
static void close_watchdog(int fd, const char *dev)
{
	int ret = write(fd, "V", 1);
	if (ret != 1)
		warn("%s: Writing magic close sequence failed."
			" The driver may not stop the watchdog", dev);
	if (close(fd))
		warn("%s: close(%i) failed", dev, fd);
}

int main(int argc, char **argv)
{
	int fd;			/* File handler for HW watchdog */
	int bootstatus;		/* HW Watchdog last boot status */
	char *dev = WATCHDOGDEV;/* HW Watchdog default device file */

	int next_option;	/* getopt iteration var */
	int wd_timeout;		/* when HW watchdog goes balistic */
	int interval = 0;	/* user parameter for wd_timeout */
	int ret = 0;		/* write/sleep call return value */

	/* Parse options if any */
	do {
		next_option = getopt_long(argc, argv, short_options,
					long_options, NULL);
		switch (next_option) {
		case 'h':
			print_usage(stdout, argv[0], EXIT_SUCCESS);
		case 'c':
			terminated = 1;
			break;
		case 'd':
			dev = optarg;
			break;
		case 'i':
			interval = atoi(optarg);
			if (interval < MIN_WD_TIMEOUT) {
				warnx("Interval %d is too small; must be at least %d",
					interval, MIN_WD_TIMEOUT);
				print_usage(stderr, argv[0], -EINVAL);
			}
			break;
		case '?':	/* Invalid options */
			print_usage(stderr, argv[0], -EINVAL);
		case -1:	/* Done with options */
			break;
		default:	/* Unexpected stuffs */
			abort();
		}
	} while (next_option != -1);

	/* Once the watchdog device file is open, the watchdog will
	 * be activated by the driver.
	 */
	fd = open(dev, O_RDWR|O_CLOEXEC);
	if (-1 == fd)
		err(EXIT_FAILURE, "open(%s) failed", dev);

	signal(SIGTERM, daisydog_sigterm);
	signal(SIGHUP, daisydog_sigterm);
	signal(SIGINT, daisydog_sigterm);

	/* If user wants to change the HW watchdog timeout. */
	if (interval) {
		if (ioctl(fd, WDIOC_SETTIMEOUT, &interval) != 0) {
			err(EXIT_FAILURE, "could not set HW watchdog"
				"interval to %d", interval);
		}
	}

	/* Get/Display current HW watchdog interval.
	 * Let user know if it's not exactly what they specified.
	 */
	if (ioctl(fd, WDIOC_GETTIMEOUT, &wd_timeout) == 0) {
		printf("HW watchdog interval is %d seconds",
				wd_timeout);

		if (interval && interval != wd_timeout)
			printf(" (user asked for %d seconds)",
					interval);

		printf("\n");

		if (wd_timeout < MIN_WD_TIMEOUT) {
			warnx("Existing HW watchdog interval %d is below "
				"required %d minimum, changing to %d",
				wd_timeout, MIN_WD_TIMEOUT, DEFAULT_INTERVAL_SECS);
			interval = DEFAULT_INTERVAL_SECS;
			if (ioctl(fd, WDIOC_SETTIMEOUT, &interval) != 0) {
				err(EXIT_FAILURE, "could not set HW watchdog"
					" interval to %d", DEFAULT_INTERVAL_SECS);
			}
		}
	} else {
		err(EXIT_FAILURE, "cannot read HW watchdog interval");
	}

	/* Check if last boot is caused by HW watchdog. */
	if (ioctl(fd, WDIOC_GETBOOTSTATUS, &bootstatus) == 0) {

		printf("%s reported boot status: ", dev);

		if (bootstatus == 0)
			printf("normal-boot");
		else if (bootstatus == -1)
			printf("UNKNOWN");
		else {
			/* Show hex value in case unknown bits are set. */
			printf("%#0x", bootstatus);

			if (bootstatus & WDIOF_CARDRESET)
				printf(" watchdog-timeout");
			if (bootstatus & WDIOF_OVERHEAT)
				printf(" CPU-overheat");
			if (bootstatus & WDIOF_POWERUNDER)
				printf(" power-undervoltage");
			if (bootstatus & WDIOF_POWEROVER)
				printf(" power-overvoltage");
			if (bootstatus & WDIOF_FANFAULT)
				printf(" fan-fault");

		}
		printf("\n");
	} else {
		err(EXIT_FAILURE, "%s: cannot read boot status", dev);
	}

	/* Before we start the main loop, release any caches we don't need. */
	malloc_trim(0);

	/* Flush out any buffered writes so they can be captured by logger. */
	fflush(NULL);

	while (!terminated) {
		ret = ioctl(fd, WDIOC_KEEPALIVE, 0);

		/* Force immediate exit of loop if keepalive fails. */
		if (ret) {
			warn("Terminating");
			ret = EXIT_FAILURE;
			break;
		}

		/* Check terminate again in case of interruption after entering the loop. */
		if (!terminated)
			sleep(PETTING_SECS);

		/* SIGTERM/HUP/INT will cause sleep(3) to return early.
		 * SIGKILL will exit anyway.
		 * If something else caused us to return early, just
		 * pretend it was a hiccup and keep looping.
		 */
	}

	close_watchdog(fd, dev);

	fflush(NULL);
	exit(ret);
}
