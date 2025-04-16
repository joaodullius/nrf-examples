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
static uint16_t remote_led_char_handle = 0;  // <- novo



static void write_led_state_cb(struct bt_conn *conn, uint8_t err,
                               struct bt_gatt_write_params *params)
{
    if (err == 0) {
        LOG_INF("Write successful");
    } else {
        LOG_ERR("Write failed, ATT error: 0x%02X", err);
    }
}

void central_role_write_led(uint8_t value)
{
    if (!default_conn || remote_led_char_handle == 0) {
        LOG_WRN("Cannot write: no connection or handle");
        return;
    }

    static struct bt_gatt_write_params write_params;

    write_params.handle = remote_led_char_handle;
    write_params.offset = 0;
    write_params.data = &value;
    write_params.length = sizeof(value);
    write_params.func = write_led_state_cb;

    int err = bt_gatt_write(default_conn, &write_params);
    if (err) {
        LOG_ERR("bt_gatt_write() failed (err %d)", err);
    } else {
        LOG_INF("Sent LED write with value: %d", value);
    }
}

uint16_t central_role_get_remote_led_handle(void)
{
    return remote_led_char_handle;
}

static void auth_passkey_display(struct bt_conn *conn, unsigned int passkey)
{
    char addr[BT_ADDR_LE_STR_LEN];
    bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
    LOG_INF("Passkey for %s: %06u", addr, passkey);
}

static void auth_passkey_entry(struct bt_conn *conn)
{
    char addr[BT_ADDR_LE_STR_LEN];
    bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
    LOG_INF("Enter passkey for %s", addr);
    
    // Use the correct function to enter the passkey
    bt_conn_auth_passkey_entry(conn, 123456);
}

static void auth_cancel(struct bt_conn *conn)
{
    char addr[BT_ADDR_LE_STR_LEN];
    bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
    LOG_INF("Pairing cancelled: %s", addr);
}

static void auth_passkey_confirm(struct bt_conn *conn, unsigned int passkey)
{
    char addr[BT_ADDR_LE_STR_LEN];
    bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
    LOG_INF("Confirm passkey for %s: %06u", addr, passkey);
    
    // Auto-confirm the passkey
    bt_conn_auth_passkey_confirm(conn);
}

static struct bt_conn_auth_cb conn_auth_callbacks = {
    .passkey_display = auth_passkey_display,
    .passkey_entry = auth_passkey_entry,
    .passkey_confirm = auth_passkey_confirm,
    .cancel = auth_cancel,
};


static void discovery_completed_cb(struct bt_gatt_dm *dm,
                                   void *context)
{
    LOG_INF("The discovery procedure succeeded");

    bt_gatt_dm_data_print(dm);

    const struct bt_gatt_dm_attr *attr = bt_gatt_dm_char_by_uuid(dm,
        BT_UUID_DECLARE_128(LED_STATE_CHAR_UUID));
    if (!attr) {
        LOG_ERR("Characteristic not found");
        bt_gatt_dm_data_release(dm);
        return;
    }

    // Get the characteristic value structure
    struct bt_gatt_chrc *chrc = bt_gatt_dm_attr_chrc_val(attr);
    if (!chrc) {
        LOG_ERR("Failed to get characteristic value");
        bt_gatt_dm_data_release(dm);
        return;
    }

    // Access the value handle from the characteristic
    uint16_t handle = chrc->value_handle;
    LOG_INF("LED State handle: 0x%04x", handle);

    remote_led_char_handle = chrc->value_handle;

    bt_gatt_dm_data_release(dm);
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

     // Força elevação de segurança
     err = bt_conn_set_security(conn, BT_SECURITY_L2); 
     if (err) {
         LOG_WRN("Failed to set security (err %d)", err);
     }

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

static void security_changed(struct bt_conn *conn, bt_security_t level,
                             enum bt_security_err err)
{
    char addr[BT_ADDR_LE_STR_LEN];
    bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

    if (!err) {
        LOG_INF("Security changed: %s level %u", addr, level);
    } else {
        LOG_INF("Security failed: %s level %u err %d", addr, level, err);
    }
}


BT_CONN_CB_DEFINE(conn_callbacks) = {
	.connected = connected,
	.disconnected = disconnected,
    .security_changed = security_changed,
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

    bt_conn_auth_cb_register(&conn_auth_callbacks);

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
 