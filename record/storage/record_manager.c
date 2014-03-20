/*
 * storage_manager.c
 *
 *  Created on: Mar 11, 2014
 *      Author: lifeng
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <unistd.h>
#include <fcntl.h>
#include <sys/mount.h>
#include <pthread.h>
#include <sys/time.h>

#include "record_manager.h"
#include "record_dic.h"
#include "record.h"
#include "../lib/crc16.h"
#include "../dump/dump_manager.h"
#include "../dump/dump.h"

#include "../lib/block_manager.h"
#include "../lib/block_filter.h"
#include "../led/led.h"

#define PARTITION_NAME	("/dev/mmcblk0p3")

#define GAP_SIZE	(256*1024) //必须大于RECORD_DATA_SIZE

const char SERIAL_TAG[3] = "SER";

static struct record_manager* gpmanager = NULL;

static void get_partition_info(int * blk_size, int *blk_num) {
	int dev = -1;
	dev = open(PARTITION_NAME, O_EXCL | O_RDWR);
	if (dev < 0)
		return;
	ioctl(dev, BLKSSZGET, blk_size);
	ioctl(dev, BLKGETSIZE, blk_num);
	close(dev);

}

static int init_storage(struct record_manager *manager) {
	FILE * file;
	int val1, val2;
	int sec_size = 0;
	int sec_num = 0;
	file = fopen(PARTITION_NAME, "rb+");

	manager->file = file;

	if (file == NULL)
		return 1;

	fread(manager->dics, sizeof(struct record_dic), 2, file);
	fread(manager->date_tbl, 512, 4, file); //read date table
	val1 = record_dic_valid(manager->dics);
	val2 = record_dic_valid(manager->dics + 1);
	if (val1 != 0) {
		if (val2 == 0) {
			bcopy(manager->dics + 1, manager->dics, sizeof(struct record_dic));
		} else {
			get_partition_info(&sec_size, &sec_num);
			init_record_dic(manager->dics, sec_size, sec_num);
			init_record_dic(manager->dics + 1, sec_size, sec_num);
			fseek(file, 0, SEEK_SET);
			fwrite(manager->dics, sizeof(struct record_dic), 1, file);
			fflush(file);
			fwrite(manager->dics + 1, sizeof(struct record_dic), 1, file);
			fflush(file);
		}
	}

	return 0;

}

void request_serial_data(struct record_manager*manager, char *data, int length) {

	struct block_filter * filter = manager->manager.fliters;

	struct block * pblock = get_block(filter, 0, BLOCK_EMPTY);
	struct record * precord;
	if (pblock != NULL) {
		precord = (struct record*) pblock->data;
		precord->header.type = (char) RECORD_DUMP_SERIAL;
		precord->header.tag[0] = 'D';
		precord->header.tag[1] = 'M';
		precord->header.tag[2] = 'P';
		precord->header.content_size = length;

		//memset(precord->data, 0, RECORD_DATA_SIZE);
		bcopy(data, precord->data, length);

		pblock->data_length = sizeof(struct record);
		put_block(filter, pblock, BLOCK_FULL);
	} else {
		printf("request_serial_data err\n");
	}
}

void store_serial_data(struct record_manager * manager, char *data, int length) {
	struct timeval tv;
	struct tm* ptm;
	long milliseconds;
	struct block * pblock;
	struct record * precord;
	struct block_filter * filter = manager->manager.fliters;
	if (filter == NULL)
		return;
	pblock = get_block(filter, 0, BLOCK_EMPTY);
	if (pblock != NULL) {
		precord = (struct record*) pblock->data;
		precord->header.type = (char) RECORD_SERIAL;
		precord->header.tag[0] = SERIAL_TAG[0];
		precord->header.tag[1] = SERIAL_TAG[1];
		precord->header.tag[2] = SERIAL_TAG[2];
		precord->header.content_size = length;

		/* 获得日期时间，并转化为 struct tm。 */
		gettimeofday(&tv, NULL);
		ptm = localtime(&tv.tv_sec);
		milliseconds = tv.tv_usec / 1000;

		precord->header.year = ptm->tm_year - 100;
		precord->header.month = ptm->tm_mon + 1;
		precord->header.day = ptm->tm_mday;
		precord->header.hour = ptm->tm_hour;
		precord->header.minute = ptm->tm_min;
		precord->header.second = ptm->tm_sec;
		precord->header.millsec = milliseconds;
		memset(precord->data, 0, RECORD_DATA_SIZE);
		bcopy(data, precord->data, length);

		put_block(filter, pblock, BLOCK_FULL);
	} else {
		//printf("store_serial_data err\n");
	}

}
struct block_filter * get_record_filter(struct record_manager *manager) {

	return manager->manager.fliters;
}

static void store_date_table(struct record_manager *manager, int index) {
	void* data = manager->date_tbl[index];
	int offset = 0;
	if (index < 0 || index > 2)
		return;

	if (index == 0) {
		offset = manager->dics[0].stat_date_offset;
	} else if (index == 1) {
		offset = manager->dics[0].serl_date_offset;
	} else {
		offset = manager->dics[0].wav_date_offset;
	}
	fseek(manager->file, offset, SEEK_SET);
	fwrite(data, 512, 1, manager->file);
	fflush(manager->file);

}

static void store_dictionary(struct record_manager *manager) {

	struct record_dic* dic = manager->dics;
	dic->crc = compute_crc16(dic, 510);
	fseek(manager->file, 0, SEEK_SET);
	fwrite(dic, 512, 1, manager->file);
	fflush(manager->file);
	fwrite(dic, 512, 1, manager->file);
	fflush(manager->file);
}

static void flush_status_data(struct record_manager *manager) {
}

static void flush_serial_data(struct record_manager *manager) {

	struct record_dic* dic = manager->dics;
	int size = manager->serial_length;
	if (size <= 0)
		return;
	light_on(2);
	//对日期的处理
	int date_changed = 0;
	int index = 0;
	struct record *record;
	int record_size = 0;
	int cur_date = 0;
	int date_pos = 0;
	int date_val = 0;
	if (manager->dics[0].serl_date_size > 0) {
		date_pos = (manager->dics[0].serl_date_next_off
				+ manager->dics[0].serl_date_total - 8)
				% (manager->dics[0].serl_date_total);
		cur_date = manager->date_tbl[1][date_pos / 4];
	}
	while (index < size) {
		record = (struct record*) (manager->serial_buffer + index);

		if (strncmp(record->header.tag, SERIAL_TAG, 3) != 0) {
			break;
		}
		if (record->header.type != 1) {
			break;
		}

		date_val = (record->header.year) | ((record->header.month) << 8)
				| ((record->header.day) << 16);
		record_size = (record->header.content_size + 16 + (16 - 1)) / 16 * 16;

		if (date_val != cur_date) {

			date_changed = 1;

			date_pos = manager->dics[0].serl_date_next_off;
			manager->date_tbl[1][date_pos / 4] = date_val;
			manager->date_tbl[1][date_pos / 4 + 1] =
					(manager->dics[0].serl_next_off + index)
							% (manager->dics[0].serl_total); //偏移量
			manager->dics[0].serl_date_next_off =
					(manager->dics[0].serl_date_next_off + 8)
							% (manager->dics[0].serl_date_total);
			manager->dics[0].serl_date_size = (manager->dics[0].serl_date_size
					+ 8) % (manager->dics[0].serl_date_total);

		}
		cur_date = date_val;
		index += record_size;

	}
	if (date_changed) {
		store_date_table(manager, 1);
	}

	int blank_size = dic->serl_total - dic->serl_next_off;
	int size1 = size < blank_size ? size : blank_size;
	int size2 = size - size1;
	if (dic->serl_total - dic->serl_size > GAP_SIZE) {

		if (size1 > 0) {
			fseek(manager->file, dic->serl_offset + dic->serl_next_off,
			SEEK_SET);
			fwrite(manager->serial_buffer, 1, size1, manager->file);
		}
		if (size2 > 0) {
			fseek(manager->file, 0,
			SEEK_SET);
			fwrite(manager->serial_buffer + size1, 1, size2, manager->file);
			dic->serl_next_off = size2;
		} else {
			dic->serl_next_off = dic->serl_next_off + size;
		}
		dic->serl_size = dic->serl_size + size;
	} else {
		//first create a gap eare

	}
	manager->serial_length = 0;
	store_dictionary(manager);

}

static void process_serial_data(struct record_manager *manager,
		struct record * record) {
	int size = 0;
	size = (record->header.content_size + (16 - 1)) / 16 * 16 + 16; //16字节对齐
	if (manager->serial_length + size > SERIAL_BUFFER_SIZE) {
		//保存串口数据数据
		flush_serial_data(manager);
	}

	bcopy(record, (manager->serial_buffer) + (manager->serial_length), size);
	manager->serial_length = manager->serial_length + size;
}

static void dump_serial_data(struct record_manager *manager,
		struct record * record) {
	static char dump_buffer[DUMP_SIZE]; //
	int i;
	int size, offset, rd_size;
	struct dump_manager *pdump_manager = NULL;
	pdump_manager = (struct dump_manager*) record->data;
	flush_serial_data(manager);

	send_dump(pdump_manager, (int) DUMP_SERIAL_ACK, 0, dump_buffer, 16);

	usleep(100000);



	if (pdump_manager->acordding_time) //根据日期存储
	{

	} else {

	}
	size = manager->dics[0].serl_size;
	offset = manager->dics[0].serl_offset
			+ (manager->dics[0].serl_next_off + manager->dics[0].serl_total
					- manager->dics[0].serl_size)
					% (manager->dics[0].serl_total);

	fseek(manager->file, offset, SEEK_SET);
	i=0;
	while (size > 0) {
		rd_size = size > DUMP_SIZE ? DUMP_SIZE : size;
		fread(dump_buffer, rd_size, 1, manager->file);
		send_dump(pdump_manager, (int) DUMP_SERIAL_DATA, i, dump_buffer,
				rd_size);
		size -= rd_size;
		i++;

	}
	send_dump(pdump_manager, (int) DUMP_SERIAL_FINISHED, 0, dump_buffer,
		DUMP_SIZE);

}

static void process_record(struct record_manager *manager,
		struct record * record) {

	switch (record->header.type) {
	case 0:
		break;
	case (char) RECORD_SERIAL:

		process_serial_data(manager, record);
		break;
	case (char) RECORD_DUMP_SERIAL: //开始转储串口数据
		dump_serial_data(manager, record);
		break;

	}
}

static void * store_proc(void *args) {
	struct block *pblock;
	struct record * record;
	struct record_manager *manager = (struct record_manager *) args;
	int ret = init_storage(manager);
	if (ret != 0)
		return NULL;
	struct block_filter * filter = get_record_filter(manager);

	while (1) {
		pblock = get_block(filter, 5000, BLOCK_FULL);
		if (pblock != NULL) {
			record = (struct record*) pblock->data;
			process_record(manager, record);

			put_block(filter, pblock, BLOCK_EMPTY);
		} else {
			flush_status_data(manager);
			flush_serial_data(manager);
		}
	}

	return NULL;

}

struct record_manager* get_record_manager() {
	if (gpmanager == NULL) {
		gpmanager = malloc(sizeof(struct record_manager));

		if (gpmanager != NULL) {
			memset(gpmanager,0,sizeof(struct record_manager));
			add_block_filter(&(gpmanager->manager), NULL, sizeof(struct record),
					20);
			gpmanager->file = NULL;
			gpmanager->serial_length = 0;
			pthread_create(&(gpmanager->thread_store), NULL, store_proc,
					gpmanager);
		}
	}
	return gpmanager;
}

void start_record() {
	get_record_manager();
}

