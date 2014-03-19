/*
 * led.c
 *
 *  Created on: Mar 19, 2014
 *      Author: lifeng
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/time.h>
#include <pthread.h>

#include "led.h"

#define TIMEOUT_MS	(100)

#ifdef __x86_64
const char com_name[] = "/dev/null";
const char gps_name[] = "/dev/null";
const char record_name[] = "/dev/null";
#else
const char com_name[] = "/sys/class/leds/com/brightness";
const char gps_name[] = "/sys/class/leds/gps/brightness";
const char record_name[] = "/sys/class/leds/record/brightness";
#endif


static void * led_proc(void *args);

static struct led* led_list = NULL;
static pthread_t thread_led;



void light_on(int no) {
	struct led* pled = led_list;
	char buffer[4];
	while (pled != NULL) {
		if (pled->led_no == no) {

			if (pled->fd > 0) {

				buffer[0] = '1';
				write(pled->fd, buffer, 1);
				pled->time_out = 3;

			}
			break;
		}
		pled = pled->next;
	}
}

static void add_led(const char *name, int no) {
	int fd = 0;
	struct led *pled = malloc(sizeof(struct led));
	if (pled != NULL) {
		pled->time_out = 0;
		fd = open(name, O_WRONLY);
		if (fd < 0) {
			free(pled);
			return;
		}
		pled->fd = fd;
		pled->led_no = no;
		pled->next = led_list;
		led_list = pled;
	}
}

static void * led_proc(void *args) {
	struct led* pled = led_list;
	char buffer[4];

	while (1) {
		usleep(TIMEOUT_MS * 1000);
		pled = led_list;
		while (pled != NULL) {
			if (pled->time_out > 0) {
				pled->time_out--;
				if (pled->time_out == 0) {
					buffer[0] = '0';
					write(pled->fd, buffer, 1);

				}
			}
			pled = pled->next;
		}
	}
	return NULL;

}
void init_led() {

	add_led(com_name, 0);
	add_led(gps_name, 1);
	add_led(record_name, 2);

	pthread_create(&thread_led,NULL,led_proc,NULL);
}

