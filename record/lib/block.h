/*
 * block.h
 *
 *  Created on: Mar 15, 2014
 *      Author: lifeng
 */

#ifndef BLOCK_H_
#define BLOCK_H_


struct block
{
	struct block* next;
	struct block* prev;
	int block_no; //block 编号
	int block_size;
	int data_length;

	char * data;
};



#endif /* BLOCK_H_ */
