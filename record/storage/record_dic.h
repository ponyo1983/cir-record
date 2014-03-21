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
	struct {
		char tag[16];
		int date_offset;
		int date_total;
		int date_next_off;
		int date_size;
		int offset;
		int total;
		int next_off; //下一个写入的偏移地址
		int size; //保存的数据的长度
		int last2_pos;
		int last1_pos;
		char r1[8];
	} sections[3];
	unsigned int last_wav[5]; //最近5条的录音记录
	char reserved[512 - 64 * 4 - 20 - 2];
	unsigned short crc;

}__attribute__((packed));

int record_dic_valid(struct record_dic *dic);
void init_record_dic(struct record_dic *dic, int sec_size, int sec_num);

#endif /* RECORD_DIC_H_ */
