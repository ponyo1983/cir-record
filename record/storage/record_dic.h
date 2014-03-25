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
		unsigned int date_offset;
		unsigned int date_total;
		unsigned int date_next_off;
		unsigned int date_size;
		unsigned int offset;
		unsigned int total;
		unsigned int next_off; //下一个写入的偏移地址
		unsigned int size; //保存的数据的长度
		unsigned int last2_pos;
		unsigned int last1_pos;
		char r1[8];
	} sections[3];
	int last_wav[5]; //最近5条的录音记录
	char reserved[512 - 64 * 4 - 20 - 2];
	unsigned short crc;

}__attribute__((packed));

int record_dic_valid(struct record_dic *dic);
void init_record_dic(struct record_dic *dic, int sec_size, int sec_num);

#endif /* RECORD_DIC_H_ */
