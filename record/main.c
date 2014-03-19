/*
 * main.c
 *
 *  Created on: Mar 10, 2014
 *      Author: lifeng
 */

#include <stdio.h>
#include <stdlib.h>

#include <unistd.h>
#include <libgen.h>

#include <pthread.h>

#include "serial/frame_manager.h"
#include "serial/frame.h"

#include "storage/record_dic.h"

extern void start_serial();
extern void start_record();
extern void start_dump();

int main(int argc, char **argv) {

#ifdef __x86_64

	init_led();

	start_serial(); //开启串口
	start_record(); //开启记录
	start_dump(); //开启转储

	while (1) {
		sleep(1);
	}

#else

	if (daemon(1, 1) == -1) {
		exit(-1);
		perror("daemon error\r\n");
	}
	chdir(dirname(argv[0])); //change current dir to application dir

	init_led();
	start_serial();//开启串口
	start_record();//开启记录
	start_dump();//开启转储

	pthread_exit(NULL);
#endif

	exit(0);
}
