/*
 * Copyright (c) 2012-2014 Wind River Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/pm/device.h>

void main(void)
{
	const struct device *uart_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_console));
	printk("Hello World! %s\n", CONFIG_BOARD);

	if (!device_is_ready(uart_dev)) {
		return;
	}

	while(1)
	{
		pm_device_action_run(uart_dev, PM_DEVICE_ACTION_SUSPEND);
		k_sleep(K_MSEC(2000));
		printk("Should not be printed, since uart0 is disabled! %s\n", CONFIG_BOARD);
		pm_device_action_run(uart_dev, PM_DEVICE_ACTION_RESUME);
		k_sleep(K_MSEC(2000));
		printk("Should be printed, since uart0 is enabled! %s\n", CONFIG_BOARD);
	}	
}
