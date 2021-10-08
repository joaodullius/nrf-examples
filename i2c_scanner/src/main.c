/*
 * Copyright (c) 2020 Sigurd Nevstad
 * Copyright (c) 2021 Joao Dullius
 * Based on:
 * https://github.com/sigurdnev/ncs-playground/tree/master/samples/i2c_scanner
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr.h>
#include <sys/printk.h>
#include <drivers/i2c.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <drivers/gpio.h>

#define I2C_DEV DT_NODELABEL(arduino_i2c)

void main(void)
{
	const struct device *i2c_dev;
	
	k_sleep(K_SECONDS(1));

	printk("Starting i2c scanner at %s\n", DT_LABEL(I2C_DEV));

	i2c_dev = device_get_binding(DT_LABEL(I2C_DEV));
	if (!i2c_dev) {
		printk("I2C: Device driver not found.\n");
		return;
	}
	
	uint8_t error = 0u;
	
	i2c_configure(i2c_dev, I2C_SPEED_SET(I2C_SPEED_STANDARD));
	
	for (uint8_t i = 4; i <= 0x77; i++) {
		struct i2c_msg msgs[1];
		uint8_t dst = 1;

		/* Send the address to read from */
		msgs[0].buf = &dst;
		msgs[0].len = 1U;
		msgs[0].flags = I2C_MSG_WRITE | I2C_MSG_STOP;
		
		error = i2c_transfer(i2c_dev, &msgs[0], 1, i);
		if (error == 0) {
			printk("0x%2x FOUND\n", i);
		}
		else {
			//printk("error %d \n", error);
		}
		
	}
	printk("Scanning done\n");
	
}