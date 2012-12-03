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
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <getopt.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>

#include <linux/watchdog.h>

#define WATCHDOGDEV "/dev/watchdog"
#define MIN_WD_TIMEOUT 5	/* WD must be at least 5 seconds */

static const char short_options[] = "hd:i:";
static const struct option long_options[] = {
	{"help", 0, NULL, 'h'},
	{"dev", 1, NULL, 'd'},
	{"interval", 1, NULL, 'i'},
	{NULL, 0, NULL, 0},
};

static void print_usage(FILE * stream, char *app_name, int exit_code)
{
	fprintf(stream, "Usage: %s [options]\n", app_name);
	fprintf(stream,
" -h  --help                Display this usage information.\n"
" -d  --dev <device_file>   Use <device_file> as HW watchdog device file.\n"
"                           The default is '/dev/watchdog'\n"
" -i  --interval <interval> Change the HW watchdog interval time\n"
"                           Must be at least %d seconds\n", MIN_WD_TIMEOUT
	);

	exit(exit_code);
}

int main(int argc, char **argv)
{
	int fd;			/* File handler for HW watchdog */
	int bootstatus;		/* Wathdog last boot status */
	char *dev = WATCHDOGDEV;/* Watchdog default device file */

	int next_option;	/* getopt iteration var */
	int wd_timeout;		/* when HW watchdog goes balistic */
	int interval = 0;	/* user parameter for wd_timeout */
	int pet_interval;	/* how often we should pet the HW watchdog */
	int slept_well;		/* return of sleep call */

	/* Parse options if any */
	do {
		next_option = getopt_long(argc, argv, short_options,
					long_options, NULL);
		switch (next_option) {
		case 'h':
			print_usage(stdout, argv[0], EXIT_SUCCESS);
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
	if (-1 == fd) {
		fprintf(stderr, "Error: %s\n", strerror(errno));
		exit(EXIT_FAILURE);
	}

	/* If user wants to change the HW watchdog timeout */
	if (interval) {
		if (ioctl(fd, WDIOC_SETTIMEOUT, &interval) != 0) {
			fprintf(stderr, "Error: Could not set HW watchdog"
					"interval to %d\n", interval);
			exit(EXIT_FAILURE);
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
		fprintf(stderr, "Error: Cannot read HW watchdog interval\n");
		exit(EXIT_FAILURE);
	}

	/* Adjust petting interval to give some "margin of error" vs
	 * when the HW watchdog will fire.
	 */
	pet_interval = wd_timeout / 2;

	/* Check if last boot is caused by HW watchdog */
	if (ioctl(fd, WDIOC_GETBOOTSTATUS, &bootstatus) == 0) {
		fprintf(stdout, "Last boot is caused by : %s\n",
			(bootstatus != 0) ? "Watchdog" : "Power-On-Reset");
	} else {
		fprintf(stderr, "Error: Cannot read watchdog status\n");
		exit(EXIT_FAILURE);
	}

	/* There are two ways to pet the watchdog:
	 * 1) by writing any dummy value into watchdog device file, or
	 * 2) by using IOCTL WDIOC_KEEPALIVE
	 *
	 * Use the first method since it's easier. :)
	 */
	do {
		ssize_t ret = write(fd, "w", 1);

		/* force immediate exit of loop if write fails. */
		if (ret != 1)
			slept_well = -errno;
		else
			slept_well = sleep(pet_interval);

	} while (slept_well == 0);

	fprintf(stdout, "Exiting (%d)\n", slept_well);

	/* Writing 'V' into watchdog device indicates the close/stop
	 * of the watchdog was intentional. Otherwise, debug message
	 * 'Watchdog timer closed unexpectedly' will be printed to
	 * dmesg and the system will reboot in wd_timeout seconds since
	 * the last time the watchdog was pet.
	 */
	if (write(fd, "V", 1))
		/* shut gcc up */;

	/* Closing the watchdog device will deactivate the watchdog. */
	close(fd);

	exit(slept_well);
}
