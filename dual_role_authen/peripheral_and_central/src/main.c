/* main.c - Application main entry point */

/*
 * Copyright (c) 2015-2016 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/types.h>
#include <stddef.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(dual_role, LOG_LEVEL_INF);

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/hci.h>

#include <zephyr/settings/settings.h>

#include "led_service.h"
#include "periph_role.h"
#include "central_role.h"


int main(void)
{
	int err;

	LOG_INF("Starting Peripheral Demo");

	periph_role_init();

	err = bt_enable(NULL);
	if (err) {
		LOG_ERR("Bluetooth init failed (err %d)", err);
	}

#ifdef CONFIG_BT_SMP
	settings_load();
#endif // CONFIG_BT_SMP
	periph_bt_ready(err);
	central_role_init();
	return 0;
}
