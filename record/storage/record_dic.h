/*
 * record_dic.h
 *
 *  Created on: Mar 12, 2014
 *      Author: lifeng
 */

#ifndef RECORD_DIC_H_
#define RECORD_DIC_H_

struct record_dic {
	//64-0
	char tag[16];
	int sec_size;
	int sec_num;
	char r0[32 + 8];
	// 64-1
	char tag_stat[16];
	int stat_date_offset;
	int stat_date_total;
	int stat_date_next_off;
	int stat_date_size;
	int stat_offset;
	int stat_total;
	int stat_next_off; //下一个写入的偏移地址
	int stat_size; //保存的数据的长度
	int stat_last2_pos;
	int stat_last1_pos;
	char r1[8];
	//64-2
	char tag_serial[16];
	int serl_date_offset;
	int serl_date_total;
	int serl_date_next_off;
	int serl_date_size;
	int serl_offset;
	int serl_total;
	int serl_next_off; //下一个写入的偏移地址
	int serl_size; //保存的数据的长度
	int serl_last2_pos;
	int serl_last1_pos;
	char r2[8];
	//64-3
	char tag_wave[16];
	int wav_date_offset;
	int wav_date_total;
	int wav_date_next_off;
	int wav_date_size;
	int wav_offset;
	int wav_total;
	int wav_next_off; //下一个写入的偏移地址
	int wav_size; //保存的数据的长度
	int wav_last2_pos;
	int wav_last1_pos;
	char r3[8];

	char reserved[512 - 64 * 4 - 2];
	unsigned short crc;

}__attribute__((packed));

int record_dic_valid(struct record_dic *dic);
void init_record_dic(struct record_dic *dic, int sec_size, int sec_num);

#endif /* RECORD_DIC_H_ */
