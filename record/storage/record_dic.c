/*
 * record_dic.c
 *
 *  Created on: Mar 12, 2014
 *      Author: lifeng
 */

#include <string.h>

#include "record_dic.h"
#include "../lib/crc16.h"

#define STATS_STORE_SIZE	(2*1024*1024)

#define SIZE_1M	(1024*1024)

const char tag[] = "CIR.RECORD";
const char tag_sts[] = "STATUS";
const char tag_ser[] = "SERIAL";
const char tag_wav[] = "WAVE";

int record_dic_valid(struct record_dic *dic) {


	int ret = strncmp(dic->tag, tag, strlen(tag));
	if (ret != 0)
		return 1;

	ret = strncmp(dic->tag_stat, tag_sts, strlen(tag_sts));
	if (ret != 0)
		return 1;
	ret = strncmp(dic->tag_serial, tag_ser, strlen(tag_ser));
	if (ret != 0)
		return 1;
	ret = strncmp(dic->tag_wave, tag_wav, strlen(tag_wav));
	if (ret != 0)
		return 1;

	ret = compute_crc16(dic, 510);
	if (dic->crc != ret)
		return 1;

	return 0;

}

void init_record_dic(struct record_dic *dic, int sec_size, int sec_num) {

	int total_size = (sec_size * sec_num - 10 * SIZE_1M);

	int serial_size = (total_size / 3) / SIZE_1M * SIZE_1M; //alignment 1M byte
	int wav_size = (total_size - serial_size) / SIZE_1M * SIZE_1M; //alignment 1M byte
	int offset=512*6;
	memset(dic, 0, 512);
	strcpy(dic->tag, tag);
	dic->sec_size = sec_size;
	dic->sec_num = sec_num;
	strcpy(dic->tag_stat, tag_sts);
	dic->stat_date_offset=512*2;
	dic->stat_date_total=512;
	dic->stat_offset = offset;
	dic->stat_total = STATS_STORE_SIZE;
	strcpy(dic->tag_serial, tag_ser);
	dic->serl_date_offset=512*3;
	dic->serl_date_total=512;
	dic->serl_offset = offset + STATS_STORE_SIZE;
	dic->serl_total = serial_size;
	strcpy(dic->tag_wave, tag_wav);
	dic->wav_date_offset=512*4;
	dic->wav_date_total=512;
	dic->wav_offset = offset + STATS_STORE_SIZE+serial_size;
	dic->wav_total=wav_size;

	dic->crc=compute_crc16(dic, 510);

}

