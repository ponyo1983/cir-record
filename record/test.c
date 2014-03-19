/*
 * test.c
 *
 *  Created on: Mar 12, 2014
 *      Author: lifeng
 */

#include <stdio.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>
#include <pthread.h>

#include "lib/block_manager.h"
#include "lib/block_filter.h"

struct block_manager manager;
struct block_filter *filter = NULL;

pthread_t thread1;
pthread_t thread2;

void* proc1(void *args) {
	struct block *block = NULL;
	int i = 0;
	int start_num = -1;
	while (1) {
		usleep(5000);

		block = get_block(filter, 200, BLOCK_EMPTY);
		if (block != NULL) {
			if (start_num != block->block_no) {
				printf("snd%d\n", start_num);
			}

			start_num = (block->block_no + 1) % 100;
			if (start_num == 0) {
				printf("snd%d\n", start_num);
			}
			put_block(filter, block, BLOCK_FULL);
		}

	}
}

void* proc2(void*args) {
	struct block *block = NULL;
	int i = 0;
	int start_num = -1;
	while (1) {
		block = get_block(filter, 200, BLOCK_FULL);
		if (block != NULL) {
			if (start_num != block->block_no) {
				printf("get%d\n", start_num);

			}
			start_num = (block->block_no + 1) % 100;
			if (start_num == 0) {
				printf("snd%d\n", start_num);
			}
			put_block(filter, block, BLOCK_EMPTY);
		}

	}
}

void test() {
	manager.fliters = NULL;
	filter = add_block_filter(&manager, NULL, 512, 100);
	filter = add_block_filter(&manager, NULL, 512, 100);
	pthread_create(&thread1, NULL, proc1, NULL);
	pthread_create(&thread2, NULL, proc2, NULL);
}
