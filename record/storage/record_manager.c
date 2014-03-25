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

#include "../sound/g726.h"

#define PARTITION_NAME	("/dev/mmcblk0p3")

#define GAP_SIZE	(256*1024) //必须大于RECORD_DATA_SIZE

const char SECTION_TAG[][3] = { "STS", "SER", "WAV" };
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

void request_playback(struct record_manager*manager, int play_index,
		struct block*wav_block) {
	struct block_filter * filter = manager->manager.fliters;

	struct block * pblock = get_block(filter, 0, BLOCK_EMPTY);
	struct record * precord;
	if (pblock != NULL) {
		precord = (struct record*) pblock->data;
		precord->header.type = (char) RECORD_PLAYBACK;
		precord->header.tag[0] = 'P';
		precord->header.tag[1] = 'L';
		precord->header.tag[2] = 'Y';
		precord->header.serial_len = play_index;
		precord->wave_data = wav_block;

		put_block(pblock, BLOCK_FULL);
	} else {
		printf("request_data err\n");
	}
}

void request_data(struct record_manager*manager, int section, char *data,
		int length) {

	struct block_filter * filter = manager->manager.fliters;

	struct block * pblock = get_block(filter, 0, BLOCK_EMPTY);
	struct record * precord;
	if (pblock != NULL) {
		precord = (struct record*) pblock->data;
		precord->header.type = ((char) RECORD_DUMP_STATUS) + section;
		precord->header.tag[0] = 'D';
		precord->header.tag[1] = 'M';
		precord->header.tag[2] = 'P';
		precord->header.serial_len = length;

		//memset(precord->data, 0, RECORD_DATA_SIZE);
		bcopy(data, precord->data, length);

		pblock->data_length = sizeof(struct record);
		put_block(pblock, BLOCK_FULL);
	} else {
		printf("request_data err\n");
	}
}

void store_wave_data(struct record_manager * manager, struct block *wave_block) {

	struct record * precord;
	struct block_filter * filter = manager->manager.fliters;
	if (filter == NULL)
		return;
	struct block *pblock = get_block(filter, 0, BLOCK_EMPTY);
	if (pblock != NULL) {
		precord = (struct record*) pblock->data;
		precord->header.type = (char) RECORD_WAVE;
		precord->header.tag[0] = SECTION_TAG[2][0];
		precord->header.tag[1] = SECTION_TAG[2][1];
		precord->header.tag[2] = SECTION_TAG[2][2];
		precord->header.wave_size = pblock->data_length;

		precord->wave_data = wave_block;

		put_block(pblock, BLOCK_FULL);

	} else {
		//printf("store_serial_data err\n");
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
		precord->header.tag[0] = SECTION_TAG[1][0];
		precord->header.tag[1] = SECTION_TAG[1][1];
		precord->header.tag[2] = SECTION_TAG[1][2];
		precord->header.serial_len = length;

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

		put_block(pblock, BLOCK_FULL);
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

	offset = manager->dics[0].sections[index].date_offset;
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

static void flush_data(struct record_manager *manager, int section,
		char *data_buffer, int size) {

	//对日期的处理
	int date_changed = 0;
	int index = 0;
	struct record *record;
	int record_size = 0;
	int cur_date = 0;
	int date_pos = 0;
	int date_val = 0;
	int i;
	int align_size = 16; //对齐尺寸
	if (section == 2) {
		align_size = 512; //wav数据是512字节对齐
	}
	struct record_dic* dic = manager->dics;
	if (size <= 0)
		return;
	if (section < 0 || section > 2)
		return;

	light_on(2);

	size = (size + (align_size - 1)) / align_size * align_size;

	if (manager->dics[0].sections[section].date_size > 0) {
		date_pos = (manager->dics[0].sections[section].date_next_off
				+ manager->dics[0].sections[section].date_total - 8)
				% (manager->dics[0].sections[section].date_total);
		cur_date = manager->date_tbl[section][date_pos / 4];
	}
	while (index < size) {
		record = (struct record*) (data_buffer + index);

		if (strncmp(record->header.tag, SECTION_TAG[section], 3) != 0) {
			break;
		}
		if (record->header.type != section) {
			break;
		}

		date_val = (record->header.year) | ((record->header.month) << 8)
				| ((record->header.day) << 16);
		record_size = (record->header.serial_len + 16 + (align_size - 1))
				/ align_size * align_size;
		if (section == 2) //波形文件数据长度为4个字节
				{
			record_size = (record->header.wave_size + 16 + (align_size - 1))
					/ align_size * align_size;
		}
		if (date_val != cur_date) {

			date_changed = 1;

			date_pos = manager->dics[0].sections[section].date_next_off;
			manager->date_tbl[section][date_pos / 4] = date_val;
			manager->date_tbl[section][date_pos / 4 + 1] =
					(manager->dics[0].sections[section].next_off + index)
							% (manager->dics[0].sections[section].total); //偏移量
			manager->dics[0].sections[section].date_next_off =
					(manager->dics[0].sections[section].date_next_off + 8)
							% (manager->dics[0].sections[section].date_total);
			manager->dics[0].sections[section].date_size =
					(manager->dics[0].sections[section].date_size + 8)
							% (manager->dics[0].sections[section].date_total);

		}
		cur_date = date_val;
		index += record_size;

	}
	if (date_changed) {
		store_date_table(manager, section);
	}
	if (section == 2) //波形数据，记录最后5条的位置
			{
		for (i = 3; i >= 0; i--) {
			dic->last_wav[i + 1] = dic->last_wav[i];
		}
		dic->last_wav[0] = dic->sections[section].next_off;
	}
	int blank_size = dic->sections[section].total
			- dic->sections[section].next_off;
	int size1 = size < blank_size ? size : blank_size;
	int size2 = size - size1;
	if (dic->sections[section].total - dic->sections[section].size > GAP_SIZE) {

		if (size1 > 0) {
			fseek(manager->file,
					dic->sections[section].offset
							+ dic->sections[section].next_off,
					SEEK_SET);
			fwrite(data_buffer, 1, size1, manager->file);
		}
		if (size2 > 0) {
			fseek(manager->file, 0,
			SEEK_SET);
			fwrite(data_buffer + size1, 1, size2, manager->file);
			dic->sections[section].next_off = size2;
		} else {
			dic->sections[section].next_off = dic->sections[section].next_off
					+ size;
		}
		dic->sections[section].size = dic->sections[section].size + size;
	} else {
		//first create a gap eare

	}
	if (section == 1) {
		manager->serial_length = 0;
	}
	store_dictionary(manager);

}

static void flush_status_data(struct record_manager *manager) {
}

static void flush_wave_data(struct record_manager *manager, unsigned char *wav_buffer,
		int size) {

	flush_data(manager, 2, wav_buffer, size);
}

static void flush_serial_data(struct record_manager *manager) {

	flush_data(manager, 1, manager->serial_buffer, manager->serial_length);

}

static void process_wave_data(struct record_manager *manager,
		struct record * record) {

	struct block * pblock = (struct block *) (record->wave_data);
	struct block * tmpBlock=NULL;
	int length;
	G726_state state;
	int i;
	short *buffer;
	if (pblock != NULL) {

		//开始压缩 G.726 rate=2
		tmpBlock=get_block(pblock->filter,0,BLOCK_EMPTY);
		if(tmpBlock!=NULL)
		{
			length=(pblock->data_length-16)/2;
			buffer=(short*)tmpBlock->data;
			G726_encode((short*)(pblock->data+16),buffer,length,"1",2,0,&state);
			length=length/4;
			for(i=0;i<length;i++)
			{
				pblock->data[16+i]=buffer[i*4]|(buffer[i*4+1]<<2)|(buffer[i*4+2]<<4) |(buffer[i*4+3]<<6);
			}
			flush_wave_data(manager, pblock->data, length+16);
			put_block(tmpBlock, BLOCK_EMPTY);
		}

		put_block(pblock, BLOCK_EMPTY);
	}

}

static void process_serial_data(struct record_manager *manager,
		struct record * record) {
	int size = 0;
	size = (record->header.serial_len + (16 - 1)) / 16 * 16 + 16; //16字节对齐
	if (manager->serial_length + size > SERIAL_BUFFER_SIZE) {
		//保存串口数据数据
		flush_serial_data(manager);
	}

	bcopy(record, (manager->serial_buffer) + (manager->serial_length), size);
	manager->serial_length = manager->serial_length + size;
}

static void dump_data(struct record_manager *manager, struct record * record,
		int section) {
	static char dump_buffer[DUMP_SIZE]; //
	int i;
	int size, offset, rd_size;
	struct dump_manager *pdump_manager = NULL;
	pdump_manager = (struct dump_manager*) record->data;
	if (section == 1) {
		flush_serial_data(manager);
	}
	//printf("dum start\n");
	send_dump(pdump_manager, section * 3, 0, dump_buffer, 16); //应答

	usleep(100000);

	if (pdump_manager->acordding_time) //根据日期存储
	{

	} else {

	}
	size = manager->dics[0].sections[section].size;
	offset = manager->dics[0].sections[section].offset
			+ (manager->dics[0].sections[section].next_off
					+ manager->dics[0].sections[section].total
					- manager->dics[0].sections[section].size)
					% (manager->dics[0].sections[section].total);

	fseek(manager->file, offset, SEEK_SET);
	i = 0;
	while (size > 0) {
		rd_size = size > DUMP_SIZE ? DUMP_SIZE : size;
		fread(dump_buffer, rd_size, 1, manager->file);
		send_dump(pdump_manager, section * 3 + 1, i, dump_buffer, rd_size);
		size -= rd_size;
		i++;

	}
	send_dump(pdump_manager, section * 3 + 2, 0, dump_buffer, 1);

}

static void dump_status_data(struct record_manager *manager,
		struct record * record) {

	dump_data(manager, record, 0);
}
static void dump_serial_data(struct record_manager *manager,
		struct record * record) {

	dump_data(manager, record, 1);
}
static void dump_wave_data(struct record_manager *manager,
		struct record * record) {

	dump_data(manager, record, 2);
}

static void process_playback(struct record_manager *manager,
		struct record * record) {


	int index=record->header.serial_len;
	struct block *wav_block=(struct block*)record->wave_data;
	struct record_header *header=(struct record_header*)wav_block->data;
	header->wave_size=0;
	if(index>=0 && index <=4 && (manager->dics[0].last_wav[index]>=0))
	{


		unsigned int offset=manager->dics[0].sections[2].offset+ manager->dics[0].last_wav[index];
		int total_size=manager->dics[0].sections[2].total;
		u_char * data=wav_block->data;

		fseek(manager->file,offset,SEEK_SET);
		fread(data,16,1,manager->file);

		if(strncmp(header->tag,"WAV",3)==0 && (header->type==2))
		{
			printf("wav lengt:%d\n",header->wave_size);
			int cpsize=header->wave_size-16;

			if(cpsize>wav_block->block_size-16)
			{
				cpsize=wav_block->block_size-16;
			}
			int size1=(total_size-offset+16)>cpsize?cpsize:total_size-offset+16;
			if(size1>0)
			{
				fread(data+16,size1,1,manager->file);
			}
			if(size1<cpsize)
			{
				fseek(manager->file,manager->dics[0].sections[2].offset,SEEK_SET);
				fread(data+16+size1,cpsize-size1,1,manager->file);
			}
		}
		else
		{
			header->wave_size=0;
		}
	}





	put_block(wav_block,BLOCK_FULL);


}

static void process_record(struct record_manager *manager,
		struct record * record) {

	switch (record->header.type) {
	case (char) RECORD_STATUS:
		break;
	case (char) RECORD_SERIAL: //开始存储串口数据
		process_serial_data(manager, record);
		break;
	case (char) RECORD_WAVE: //开始处理录音数据
		process_wave_data(manager, record);
		break;
	case (char) RECORD_DUMP_STATUS:
		dump_status_data(manager, record);
		break;
	case (char) RECORD_DUMP_SERIAL: //开始转储串口数据
		dump_serial_data(manager, record);
		break;
	case (char) RECORD_DUMP_WAVE: //开始转储串口数据
		dump_wave_data(manager, record);
		break;
	case (char) RECORD_PLAYBACK: //开始回放数据
		process_playback(manager, record);
		break;
	default:
		//printf("record type:%d\n",record->header.type);
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

			put_block(pblock, BLOCK_EMPTY);
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
			memset(gpmanager, 0, sizeof(struct record_manager));
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

