/*
 * Copyright (c) 2015-2016 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "led_service.h"

#include <zephyr/types.h>
#include <stddef.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>
 
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(central_role, LOG_LEVEL_DBG);
 
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <bluetooth/gatt_dm.h>
#include <bluetooth/scan.h>

static struct bt_conn *default_conn;

static void discovery_completed_cb(struct bt_gatt_dm *dm,
				   void *context)
{
	LOG_INF("The discovery procedure succeeded");

	bt_gatt_dm_data_print(dm);

}

static void discovery_service_not_found_cb(struct bt_conn *conn,
					   void *context)
{
	LOG_ERR("The service could not be found during the discovery");
}

static void discovery_error_found_cb(struct bt_conn *conn,
				     int err,
				     void *context)
{
	LOG_ERR("The discovery procedure failed with %d", err);
}

static const struct bt_gatt_dm_cb discovery_cb = {
	.completed = discovery_completed_cb,
	.service_not_found = discovery_service_not_found_cb,
	.error_found = discovery_error_found_cb,
};

static void gatt_discover(struct bt_conn *conn)
{
	int err;

	LOG_DBG("Running Gatt Discover Function");

    static const struct bt_uuid_128 sensor_uuid =BT_UUID_INIT_128(LED_SERVICE_UUID);
	err = bt_gatt_dm_start(conn, &sensor_uuid.uuid, &discovery_cb, NULL);
		if (err) {
			LOG_ERR("Failed to start discovery (err %d)", err);
		}
}

static void connected(struct bt_conn *conn, uint8_t conn_err)
{
	char addr[BT_ADDR_LE_STR_LEN];
	int err;
    

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

    LOG_DBG("Connected to %s", addr);

	if (conn_err) {
		LOG_INF("Failed to connect to %s (%d)", addr, conn_err);

		if (default_conn == conn) {
			bt_conn_unref(default_conn);
			default_conn = NULL;

			err = bt_scan_start(BT_SCAN_TYPE_SCAN_ACTIVE);
			if (err) {
				LOG_ERR("Scanning failed to start (err %d)",
					err);
			}
		}

		return;
	}

	LOG_INF("Connected: %s", addr);
    gatt_discover(conn);
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
	char addr[BT_ADDR_LE_STR_LEN];
	int err;

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	LOG_INF("Disconnected: %s (reason %u)", addr, reason);

	if (default_conn != conn) {
		return;
	}

	bt_conn_unref(default_conn);
	default_conn = NULL;

	err = bt_scan_start(BT_SCAN_TYPE_SCAN_ACTIVE);
	if (err) {
		LOG_ERR("Scanning failed to start (err %d)",
			err);
	}
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
	.connected = connected,
	.disconnected = disconnected,
};

static void scan_connecting_error(struct bt_scan_device_info *device_info)
{
	LOG_WRN("Connecting failed");
}

static void scan_connecting(struct bt_scan_device_info *device_info,
			    struct bt_conn *conn)
{
    LOG_DBG("Connecting...");
	default_conn = bt_conn_ref(conn);
}

static void scan_filter_no_match(struct bt_scan_device_info *info,
    bool connectable)
{
char addr[BT_ADDR_LE_STR_LEN];
bt_addr_le_to_str(info->recv_info->addr, addr, sizeof(addr));

LOG_INF("No filter match: %s", addr);
LOG_HEXDUMP_INF(info->adv_data->data, info->adv_data->len, "ADV payload");
}

static void scan_filter_match(struct bt_scan_device_info *info,
    struct bt_scan_filter_match *match, bool connectable)
{  
    char addr[BT_ADDR_LE_STR_LEN];
    bt_addr_le_to_str(info->recv_info->addr, addr, sizeof(addr));
    LOG_DBG("Filter match found %s", addr);

    return;
}

uint8_t scanning_state = 1;

int sensor_scanner_stop(void) {
    int err = bt_scan_stop();
    if (err == 0) {
        scanning_state = 0;
        LOG_INF("Scanning stopped");
    } else {
        LOG_ERR("Failed to stop scanning (err %d)", err);
    }
    return err;
}

int sensor_scanner_start(void) {
    int err = bt_scan_start(BT_SCAN_TYPE_SCAN_ACTIVE);
    if (err == 0) {
        scanning_state = 1;
        LOG_INF("Scanning started");
    } else {
        LOG_ERR("Failed to start scanning (err %d)", err);
    }
    return err;
}

BT_SCAN_CB_INIT(scan_cb, scan_filter_match, NULL, scan_connecting_error, scan_connecting);

static struct bt_le_scan_param scan_param = {
    .type = BT_LE_SCAN_TYPE_ACTIVE,
    .options = BT_LE_SCAN_OPT_NONE,
    .interval = BT_GAP_SCAN_FAST_INTERVAL,
    .window = BT_GAP_SCAN_FAST_WINDOW,
};



 static int scan_init(void)
{
    int err;
    struct bt_scan_init_param init = {
        .connect_if_match = 1,
        .scan_param = &scan_param,
    };
    bt_scan_init(&init);
    bt_scan_cb_register(&scan_cb);


    static const struct bt_uuid_128 sensor_uuid = BT_UUID_INIT_128(LED_SERVICE_UUID);
    char uuid_str[BT_UUID_STR_LEN];
    
    // Convert UUID to string for debugging
    bt_uuid_to_str(&sensor_uuid.uuid, uuid_str, sizeof(uuid_str));
    LOG_DBG("Adding UUID filter: %s", uuid_str);
    
    err = bt_scan_filter_add(BT_SCAN_FILTER_TYPE_UUID, &sensor_uuid.uuid);
    if (err) {
        LOG_ERR("Failed to add filter (err %d)", err);
        return err;
    }
    if (!err) {
        err = bt_scan_filter_enable(BT_SCAN_UUID_FILTER, false);
        if (err) {
            LOG_ERR("Failed to enable filter (err %d)", err);
        } else {
            LOG_INF("Filter enabled for UUID: %s", uuid_str);
        }
    }
    return err;

}

int central_role_init(void) {
	LOG_INF("Starting Central Demo");

	int err = scan_init();
	if (err) {
		LOG_ERR("Scan init failed: %d", err);
		return err;
	}

	// Start scanning manual
	err = bt_scan_start(BT_SCAN_TYPE_SCAN_ACTIVE);
	if (err) {
		LOG_ERR("Scan start failed: %d", err);
		return err;
	}

	LOG_INF("Scan started");
	return 0;
}
 