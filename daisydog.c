/*
 * Simple Linux HW watchdog daemon
 *
 * Copyright (c) 2010 Daniel Widyanko. All rights reserved.
 * Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
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
#include <string.h>
#include <signal.h>
#include <errno.h>
#include <unistd.h>

#include <linux/watchdog.h>

#define WATCHDOGDEV "/dev/watchdog"
#define MIN_WD_TIMEOUT 5	/* WD must be at least 5 seconds */

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

int main(int argc, char **argv)
{
	int fd;			/* File handler for HW watchdog */
	int bootstatus;		/* HW Watchdog last boot status */
	char *dev = WATCHDOGDEV;/* HW Watchdog default device file */

	int next_option;	/* getopt iteration var */
	int wd_timeout;		/* when HW watchdog goes balistic */
	int interval = 0;	/* user parameter for wd_timeout */
	ssize_t ret = 0;	/* write/sleep call return value */

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
			if (interval < MIN_WD_TIMEOUT)
				print_usage(stderr, argv[0], -EINVAL);
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
	fd = open(dev, O_RDWR);
	if (-1 == fd)
		err(EXIT_FAILURE, "open(%s)", dev);

	signal(SIGTERM, daisydog_sigterm);
	signal(SIGHUP, daisydog_sigterm);
	signal(SIGINT, daisydog_sigterm);

	/* If user wants to change the HW watchdog timeout */
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
		fprintf(stdout, "HW watchdog interval is %d seconds",
				wd_timeout);

		if (interval && interval != wd_timeout)
			fprintf(stdout, " (user asked for %d seconds)\n",
					interval);

		fprintf(stdout, "\n");
	} else {
		err(EXIT_FAILURE, "cannot read HW watchdog interval");
	}

	/* Check if last boot is caused by HW watchdog */
	if (ioctl(fd, WDIOC_GETBOOTSTATUS, &bootstatus) == 0) {

		printf("%s reported boot status: ", dev);

		if (bootstatus == 0)
			puts("normal-boot");
		else if (bootstatus == -1)
			puts("UNKNOWN");
		else {
			/* show hex value in case unknown bits are set */
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

			putchar('\n');
		}

	} else {
		err(EXIT_FAILURE, "%s: cannot read boot status", dev);
	}

	/* There are two ways to pet the watchdog:
	 * 1) by writing any dummy value into watchdog device file, or
	 * 2) by using IOCTL WDIOC_KEEPALIVE
	 *
	 * Use the first method since it's easier. :)
	 */
	while (!terminated) {
		ret = write(fd, "w", 1);

		/* force immediate exit of loop if write fails. */
		if (ret != 1) {
			warn("Terminating");
			ret = EXIT_FAILURE;
			break;
		}

		sleep(PETTING_SECS);

		/* SIGTERM/HUP/INT will cause sleep(3) to return early.
		 * SIGKILL will exit anyway.
		 * If something else caused us to return early, just
		 * pretend it was a hiccup and keep looping.
		 */
	}

	/* Writing 'V' into watchdog device indicates the close/stop
	 * of the watchdog was intentional. Otherwise, debug message
	 * 'Watchdog timer closed unexpectedly' will be printed to
	 * dmesg and the system will reboot in wd_timeout seconds since
	 * the last time the watchdog was pet.
	 */
	if (write(fd, "V", 1))
		/* shut gcc up */;

	/* When we exit, the watchdog will be closed and deactivated
	 * automatically. We already flagged the closing as intentinal
	 * above, so there's no need to be explicit.  Save some space.
	 */

	exit(ret);
}
