/* main.c - Application main entry point */

/*
 * Copyright (c) 2015-2016 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "custom_auth.h"
#include "custom_auth_client.h"

#include <zephyr/types.h>
#include <stddef.h>
#include <zephyr/sys/util.h>

#include <dk_buttons_and_leds.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/gatt.h>
#include <bluetooth/gatt_dm.h>
#include <bluetooth/scan.h>

#include <zephyr/logging/log.h>

#define LOG_MODULE_NAME ble_scanner
LOG_MODULE_REGISTER(LOG_MODULE_NAME, LOG_LEVEL_DBG);

enum {
	NUS_C_INITIALIZED,
	NUS_C_BUTTOM_NOTIF_ENABLED,
	NUS_C_RX_WRITE_PENDING
};

#define CHRC_STATUS_LED         DK_LED1
#define CON_STATUS_LED			DK_LED2

#define FIXED_PASSWORD 123456

static struct bt_conn *default_conn;
static struct bt_custom_auth custom_auth;


static void write_cb(struct bt_conn *conn, uint8_t err,
                     struct bt_gatt_write_params *params)
{

	struct bt_custom_auth *custom_auth_c;

	/* Retrieve module context. */
	custom_auth_c = CONTAINER_OF(params, struct bt_custom_auth, write_params);

    if (err) {
        printk("Write failed (err %u)\n", err);
    } else {
        printk("Write complete\n");
    }
	
}

int bt_custom_auth_set_led(struct bt_custom_auth *custom_auth_c, const uint8_t data)
{
	int err;

	if (!custom_auth_c->conn) {
		return -ENOTCONN;
	}

    custom_auth_c->write_params.func = write_cb;
    custom_auth_c->write_params.handle = custom_auth_c->handles.led; /* replace with the handle of the characteristic */
    custom_auth_c->write_params.length = sizeof(data);
	custom_auth_c->write_params.offset = 0;
    custom_auth_c->write_params.data = &data;

	err = bt_gatt_write(custom_auth_c->conn, &custom_auth_c->write_params);
	if (err) {
		LOG_ERR("bt_gatt_write Error");
	}

	return err;

}


int bt_custom_auth_handles_assign(struct bt_gatt_dm *dm,
			  struct bt_custom_auth *custom_auth_c)

{
	const struct bt_gatt_dm_attr *gatt_service_attr =
			bt_gatt_dm_service_get(dm);
	const struct bt_gatt_service_val *gatt_service =
			bt_gatt_dm_attr_service_val(gatt_service_attr);
	const struct bt_gatt_dm_attr *gatt_chrc;
	const struct bt_gatt_dm_attr *gatt_desc;

	if (bt_uuid_cmp(gatt_service->uuid, BT_UUID_CUSTOM_SERVICE)) {
		return -ENOTSUP;
	}
	LOG_DBG("Getting handles from Custom Auth service.");
	memset(&custom_auth_c->handles, 0xFF, sizeof(custom_auth_c->handles));


	/* Get Led (WRITE) Chatacteristic */
	gatt_chrc = bt_gatt_dm_char_by_uuid(dm, BT_UUID_CUSTOM_LED);
	if (!gatt_chrc) {
		LOG_ERR("Missing LED Write characteristic.");
		return -EINVAL;
	}
	gatt_desc = bt_gatt_dm_desc_by_uuid(dm, gatt_chrc, BT_UUID_CUSTOM_LED);
	if (!gatt_desc) {
		LOG_ERR("Missing LED Write value descriptor in characteristic.");
		return -EINVAL;
	}
	LOG_DBG("Found handle for LED Write characteristic = 0x%x.",  gatt_desc->handle);
	custom_auth_c->handles.led = gatt_desc->handle;


	/* Get Button (READ) Characterstic and CCC*/
	gatt_chrc = bt_gatt_dm_char_by_uuid(dm, BT_UUID_BUTTON_CHRC);
	if (!gatt_chrc) {
		LOG_ERR("Missing Button Read characteristic.");
		return -EINVAL;
	}
	gatt_desc = bt_gatt_dm_desc_by_uuid(dm, gatt_chrc, BT_UUID_BUTTON_CHRC);
	if (!gatt_desc) {
		LOG_ERR("Missing Button Read value descriptor in characteristic.");
		return -EINVAL;
	}
	LOG_DBG("Found handle for BUTTON Read characteristic.");
	custom_auth_c->handles.button = gatt_desc->handle;
	/* NUS TX CCC */
	gatt_desc = bt_gatt_dm_desc_by_uuid(dm, gatt_chrc, BT_UUID_GATT_CCC);
	if (!gatt_desc) {
		LOG_ERR("Missing Button Read CCC in characteristic.");
		return -EINVAL;
	}
	LOG_DBG("Found handle for CCC of BUTTON Read characteristic.");
	custom_auth_c->handles.button_ccc = gatt_desc->handle;


	/* Assign connection instance. */
	custom_auth_c->conn = bt_gatt_dm_conn_get(dm);
	return 0;
}

static uint8_t on_received(struct bt_conn *conn,
			struct bt_gatt_subscribe_params *params,
			const void *data, uint16_t length)
{
	struct bt_custom_auth *custom_auth;

	/* Retrieve NUS Client module context. */
	custom_auth = CONTAINER_OF(params, struct bt_custom_auth, button_notif_params);

	if (!data) {
		LOG_DBG("[UNSUBSCRIBED]");
		params->value_handle = 0;
		atomic_clear_bit(&custom_auth->state, NUS_C_BUTTOM_NOTIF_ENABLED);
		if (custom_auth->cb.unsubscribed) {
			custom_auth->cb.unsubscribed(custom_auth);
		}
		return BT_GATT_ITER_STOP;
	}

	uint8_t value = *((uint8_t*)data);
	LOG_DBG("[NOTIFICATION] data %p length %u value %d", data, length, value);
		if (value == 0x01) {
        LOG_DBG("Received value: 0x01, setting LED on");
		dk_set_led_on(CHRC_STATUS_LED);

    } else if (value == 0x00) {

        LOG_DBG("Received value: 0x00, setting LED off");
		dk_set_led_off(CHRC_STATUS_LED);
	}

	if (custom_auth->cb.received) {
		return custom_auth->cb.received(custom_auth, data, length);
	}

	return BT_GATT_ITER_CONTINUE;
}

int bt_nus_subscribe_receive(struct bt_custom_auth *custom_auth_c)
{
	int err;

	if (atomic_test_and_set_bit(&custom_auth_c->state, NUS_C_BUTTOM_NOTIF_ENABLED)) {
		return -EALREADY;
	}

	custom_auth_c->button_notif_params.notify = on_received;
	custom_auth_c->button_notif_params.value = BT_GATT_CCC_NOTIFY;
	custom_auth_c->button_notif_params.value_handle = custom_auth_c->handles.button;
	custom_auth_c->button_notif_params.ccc_handle = custom_auth_c->handles.button_ccc;
	atomic_set_bit(custom_auth_c->button_notif_params.flags,
		       BT_GATT_SUBSCRIBE_FLAG_VOLATILE);

	err = bt_gatt_subscribe(custom_auth_c->conn, &custom_auth_c->button_notif_params);
	if (err) {
		LOG_ERR("Subscribe failed (err %d)", err);
		atomic_clear_bit(&custom_auth_c->state, NUS_C_BUTTOM_NOTIF_ENABLED);
	} else {
		LOG_DBG("[SUBSCRIBED]");
	}

	return err;
}


static void discovery_completed_cb(struct bt_gatt_dm *dm,
				   void *context)
{
	int err;

	printk("The discovery procedure succeeded\n");

	bt_gatt_dm_data_print(dm);

	err = bt_custom_auth_handles_assign(dm, &custom_auth);
	if (err) {
		printk("Could not init client object, error: %d\n", err);
	}

	bt_nus_subscribe_receive(&custom_auth);

	err = bt_gatt_dm_data_release(dm);
	if (err) {
		printk("Could not release the discovery data, error "
		       "code: %d\n", err);
	}
}

static void discovery_service_not_found_cb(struct bt_conn *conn,
					   void *context)
{
	printk("The service could not be found during the discovery\n");
}

static void discovery_error_found_cb(struct bt_conn *conn,
				     int err,
				     void *context)
{
	printk("The discovery procedure failed with %d\n", err);
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

	err = bt_gatt_dm_start(conn, BT_UUID_CUSTOM_SERVICE, &discovery_cb, NULL);
		if (err) {
			printk("Failed to start discovery (err %d)\n", err);
		}
}

static void connected(struct bt_conn *conn, uint8_t conn_err)
{
	char addr[BT_ADDR_LE_STR_LEN];
	int err;

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

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

	err = bt_conn_set_security(conn, BT_SECURITY_L4);
	if (err) {
		LOG_WRN("Failed to set security: %d", err);
		gatt_discover(conn);
	}

	err = bt_scan_stop();
	if ((!err) && (err != -EALREADY)) {
		LOG_ERR("Stop LE scan failed (err %d)", err);
	}
	dk_set_led_on(CON_STATUS_LED);
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
	dk_set_led_off(CON_STATUS_LED);
}

static void security_changed(struct bt_conn *conn, bt_security_t level,
			     enum bt_security_err err)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	if (!err) {
		LOG_WRN("Security changed: %s level %u", addr, level);
	} else {
		LOG_ERR("Security failed: %s level %u err %d", addr,
			level, err);
	}

	gatt_discover(conn);
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
	.connected = connected,
	.disconnected = disconnected,
	.security_changed = security_changed
};

static void scan_filter_match(struct bt_scan_device_info *device_info,
			      struct bt_scan_filter_match *filter_match,
			      bool connectable)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(device_info->recv_info->addr, addr, sizeof(addr));

	LOG_INF("Filters matched. Address: %s connectable: %d",
		addr, connectable);
}

static void scan_connecting_error(struct bt_scan_device_info *device_info)
{
	LOG_WRN("Connecting failed");
}

static void scan_connecting(struct bt_scan_device_info *device_info,
			    struct bt_conn *conn)
{
	default_conn = bt_conn_ref(conn);
}

BT_SCAN_CB_INIT(scan_cb, scan_filter_match, NULL,
		scan_connecting_error, scan_connecting);

static int scan_init(void)
{
	int err;
	struct bt_scan_init_param scan_init = {
		.connect_if_match = 1,
	};


	bt_scan_init(&scan_init);
	bt_scan_cb_register(&scan_cb);

	err = bt_scan_filter_add(BT_SCAN_FILTER_TYPE_UUID, BT_UUID_CUSTOM_SERVICE);
	if (err) {
		LOG_ERR("Scanning filters cannot be set (err %d)", err);
		return err;
	}

	err = bt_scan_filter_enable(BT_SCAN_UUID_FILTER, false);
	if (err) {
		LOG_ERR("Filters cannot be turned on (err %d)", err);
		return err;
	}

	LOG_INF("Scan module initialized");
	return err;
}


static void auth_passkey_entry(struct bt_conn *conn)
{
	char addr[BT_ADDR_LE_STR_LEN];

	default_conn = bt_conn_ref(conn);

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

    // Check for passkey entry event
    unsigned int passkey = FIXED_PASSWORD;
	LOG_INF("Passkey entered %s: %06u", addr, passkey);

        // Set passkey using bt_conn_auth_passkey_entry()
	 bt_conn_auth_passkey_entry(conn, passkey);
}

static void auth_cancel(struct bt_conn *conn)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	LOG_INF("Pairing cancelled: %s", addr);
}


static void pairing_complete(struct bt_conn *conn, bool bonded)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	LOG_INF("Pairing completed: %s, bonded: %d", addr, bonded);
}


static void pairing_failed(struct bt_conn *conn, enum bt_security_err reason)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	LOG_ERR("Pairing failed conn: %s, reason %d", addr, reason);
}



static struct bt_conn_auth_cb conn_auth_callbacks = {
	.passkey_entry = auth_passkey_entry,
	.cancel = auth_cancel,
};


static struct bt_conn_auth_info_cb conn_auth_info_callbacks = {
	.pairing_complete = pairing_complete,
	.pairing_failed = pairing_failed
};


void button_handler(uint32_t button_state, uint32_t has_changed)
{
	
	if (has_changed)
	{
		switch (has_changed)
		{
			case DK_BTN1_MSK:
				LOG_INF("Button 1 state = %d.", button_state);
				bt_custom_auth_set_led(&custom_auth, (uint8_t)button_state);
				break;
			case DK_BTN2_MSK:
				break;
			case DK_BTN3_MSK:
				break;
			case DK_BTN4_MSK:
				break;
			default:
				break;
		}
    
    }
}

void main(void)
{
	int err;

	err = dk_leds_init();
	if (err) {
		LOG_ERR("LEDs init failed (err %d)", err);
		return;
	}

	err = dk_buttons_init(button_handler);
    if (err) {
        LOG_ERR("'Co'uldn't init buttons (err %d)", err);
    }

	err = bt_conn_auth_cb_register(&conn_auth_callbacks);
	if (err) {
		LOG_ERR("Failed to register authorization callbacks.");
		return;
	}

	err = bt_conn_auth_info_cb_register(&conn_auth_info_callbacks);
	if (err) {
		LOG_ERR("Failed to register authorization info callbacks.");
		return;
	}

	err = bt_enable(NULL);

	if (err) {
		LOG_ERR("Bluetooth init failed (err %d)", err);
		return;
	}

	LOG_INF("Bluetooth initialized");

	scan_init();

	LOG_INF("Scanning successfully started");
	err = bt_scan_start(BT_SCAN_TYPE_SCAN_ACTIVE);
	if (err) {
		LOG_ERR("Scanning failed to start (err %d)", err);
		return;
	}

	LOG_INF("Scanning successfully started");
}

