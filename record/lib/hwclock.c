/*
 * hwclock.c
 *
 *  Created on: Mar 11, 2014
 *      Author: lifeng
 */

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <sys/ioctl.h>

struct linux_rtc_time {
	int tm_sec;
	int tm_min;
	int tm_hour;
	int tm_mday;
	int tm_mon;
	int tm_year;
	int tm_wday;
	int tm_yday;
	int tm_isdst;
};
#define RTC_SET_TIME   _IOW('p', 0x0a, struct linux_rtc_time) /* Set RTC time    */
#define RTC_RD_TIME    _IOR('p', 0x09, struct linux_rtc_time) /* Read RTC time   */

static time_t read_rtc(int utc) {
	int rtc;
	struct tm tm;
	char *oldtz = 0;
	time_t t = 0;
	if ((rtc = open("/dev/rtc", O_RDONLY)) < 0) {
		if ((rtc = open("/dev/misc/rtc", O_RDONLY)) < 0) {
			printf("Could not access RTC\n");
		}

	}
	memset(&tm, 0, sizeof(struct tm));
	if (ioctl(rtc, RTC_RD_TIME, &tm) < 0) {

	}
	//bb_perror_msg_and_die("Could not read time from RTC");
	tm.tm_isdst = -1; // not known
	close(rtc);
	if (utc) {
		oldtz = getenv("TZ");
		setenv("TZ", "UTC 0", 1);
		tzset();
	}
	t = mktime(&tm);
	if (utc) {
		if (oldtz)
			setenv("TZ", oldtz, 1);
		else
			unsetenv("TZ");
		tzset();
	}
	return t;
}
static void write_rtc(time_t t, int utc) {
	int rtc;
	struct tm tm;
	if ((rtc = open("/dev/rtc", O_WRONLY)) < 0) {
		if ((rtc = open("/dev/misc/rtc", O_WRONLY)) < 0) {
			printf("Could not access RTC\n");
		}

	}
	tm = *(utc ? gmtime(&t) : localtime(&t));
	tm.tm_isdst = 0;
	if (ioctl(rtc, RTC_SET_TIME, &tm) < 0) {
		printf("Could not set the RTC time\n");
	}

	close(rtc);
}



void get_time(struct tm *ptm) {
	struct tm *tm;
	time_t t;
	t = read_rtc(1);
	tm = localtime(&t); /* Sets 'tzname[]' */
	bcopy(tm,ptm,sizeof(struct tm));
}


