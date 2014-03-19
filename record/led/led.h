/*
 * led.h
 *
 *  Created on: Mar 19, 2014
 *      Author: lifeng
 */

#ifndef LED_H_
#define LED_H_


struct led
{
	int led_no;//编号
	int fd;
	int time_out;

	struct led *next;
};


void light_on(int no);

#endif /* LED_H_ */
