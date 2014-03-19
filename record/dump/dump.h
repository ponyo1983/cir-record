/*
 * dump.h
 *
 *  Created on: Mar 15, 2014
 *      Author: lifeng
 */

#ifndef DUMP_H_
#define DUMP_H_

#define DUMP_SIZE	(2*1024*1024)

enum dump_type
{
	DUMP_SERIAL_ACK=1,
	DUMP_SERIAL_DATA=2,
	DUMP_SERIAL_FINISHED=3,
};

struct dump
{

	int type;
	int size; //最大数据的大小
	int length; //实际数据大小
	int data_num; //数据编号
	char data[DUMP_SIZE];


};


#endif /* DUMP_H_ */
