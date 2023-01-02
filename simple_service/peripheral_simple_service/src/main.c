/*
 * Copyright (c) 2012-2014 Wind River Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <dk_buttons_and_leds.h>

#include <zephyr/bluetooth/bluetooth.h>

#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/hci.h>

static uint8_t button_value = 0;

static bool   notify_enabled;


#define LOG_MODULE_NAME app
LOG_MODULE_REGISTER(LOG_MODULE_NAME);

#define CHRC_STATUS_LED         DK_LED1
#define CON_STATUS_LED			DK_LED2

#define DEVICE_NAME CONFIG_BT_DEVICE_NAME
#define DEVICE_NAME_LEN (sizeof(DEVICE_NAME)-1)

#define BT_UUID_CUSTOM_SERV_VAL \
   BT_UUID_128_ENCODE(0x86b50001, 0x7ff7, 0x496e, 0xaa9c, 0x05fc11855eb3)
#define BT_UUID_CUSTOM_SERVICE  BT_UUID_DECLARE_128(BT_UUID_CUSTOM_SERV_VAL)

#define BT_UUID_CUSTOM_LED_CHAR_VAL \
   BT_UUID_128_ENCODE(0x86b50002, 0x7ff7, 0x496e, 0xaa9c, 0x05fc11855eb3)
#define BT_UUID_CUSTOM_LED  BT_UUID_DECLARE_128(BT_UUID_CUSTOM_LED_CHAR_VAL)

#define BT_UUID_BUTTON_CHRC_VAL \
   BT_UUID_128_ENCODE(0x86b50003, 0x7ff7, 0x496e, 0xaa9c, 0x05fc11855eb3)
#define BT_UUID_BUTTON_CHRC  BT_UUID_DECLARE_128(BT_UUID_BUTTON_CHRC_VAL)

#define FIXED_PASSWORD 123456

static struct bt_conn *current_conn;
static struct bt_conn *auth_conn;
static const struct bt_data ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
	BT_DATA(BT_DATA_NAME_COMPLETE, DEVICE_NAME, DEVICE_NAME_LEN),
};

static const struct bt_data sd[] = {
    BT_DATA_BYTES(BT_DATA_UUID128_ALL, BT_UUID_CUSTOM_SERV_VAL),
};

void on_sent(struct bt_conn *conn, void *user_data)
{
    ARG_UNUSED(user_data);
    LOG_INF("Notification sent on connection %p", (void *)conn);
}

static ssize_t on_write(struct bt_conn *conn, const struct bt_gatt_attr *attr,
							const void *buf, uint16_t len, uint16_t offset,
			  				uint8_t flags)
{
    uint8_t temp_str[len+1];
    memcpy(temp_str, buf, len);
    temp_str[len] = 0x00;

    //LOG_DBG("Received data on conn %p. Len: %d", (void *)conn, len);
	//LOG_HEXDUMP_DBG(temp_str, len, "Received");
	
	if (temp_str[0] == 0x01) {
        LOG_DBG("Received value: 0x01, setting LED on");
		dk_set_led_on(CHRC_STATUS_LED);

    } else if (temp_str[0] == 0x00) {

        LOG_DBG("Received value: 0x00, setting LED off");
		dk_set_led_off(CHRC_STATUS_LED);
	}
	return len;
}

void button_chrc_ccc_cfg_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
   notify_enabled = (value == BT_GATT_CCC_NOTIFY);
   LOG_INF("Notifications %s", notify_enabled? "enabled":"disabled");

}


static ssize_t read_button_chrc_cb(struct bt_conn *conn, const struct bt_gatt_attr *attr,
			 void *buf, uint16_t len, uint16_t offset)
{
	LOG_DBG("Attribute read, handle: %u, conn: %p", attr->handle,
		(void *)conn);

	return bt_gatt_attr_read(conn, attr, buf, len, offset, &button_value,
				 sizeof(button_value));
}

BT_GATT_SERVICE_DEFINE( custom_srv, 
                        BT_GATT_PRIMARY_SERVICE(BT_UUID_CUSTOM_SERVICE),

						// BUTTON
						BT_GATT_CHARACTERISTIC(	BT_UUID_BUTTON_CHRC,
                    							BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
                    							BT_GATT_PERM_READ_AUTHEN,
                    							read_button_chrc_cb,
												NULL, NULL),
						BT_GATT_CCC(button_chrc_ccc_cfg_changed, BT_GATT_PERM_READ_AUTHEN | BT_GATT_PERM_WRITE_AUTHEN),
						
						//LED
						BT_GATT_CHARACTERISTIC( BT_UUID_CUSTOM_LED,
                                                BT_GATT_CHRC_WRITE,
                                                BT_GATT_PERM_WRITE_AUTHEN,
                                                NULL, on_write, NULL),
                        );

static void connected(struct bt_conn *conn, uint8_t err)
{
	char addr[BT_ADDR_LE_STR_LEN];

	if (err) {
		LOG_ERR("Connection failed (err %u)", err);
		return;
	}

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
	LOG_INF("Connected %s", addr);

	current_conn = bt_conn_ref(conn);

	dk_set_led_on(CON_STATUS_LED);
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	LOG_INF("Disconnected: %s (reason %u)", addr, reason);

	if (auth_conn) {
		bt_conn_unref(auth_conn);
		auth_conn = NULL;
	}

	if (current_conn) {
		bt_conn_unref(current_conn);
		current_conn = NULL;
		dk_set_led_off(CON_STATUS_LED);
	}
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
}
BT_CONN_CB_DEFINE(conn_callbacks) = {
	.connected        = connected,
	.disconnected     = disconnected,
	.security_changed = security_changed,
};

static void auth_passkey_entry(struct bt_conn *conn)
{
	char addr[BT_ADDR_LE_STR_LEN];

	auth_conn = bt_conn_ref(conn);

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

	LOG_ERR("Pairing cancelled: %s", addr);
}
static void pairing_confirm(struct bt_conn *conn)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	bt_conn_auth_pairing_confirm(conn);

	LOG_INF("Pairing confirmed: %s", addr);
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

	LOG_INF("Pairing failed conn: %s, reason %d", addr, reason);

	bt_conn_disconnect(conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
}

/* Callbacks */
static struct bt_conn_auth_cb conn_auth_callbacks = {
.passkey_display = NULL,
.passkey_confirm = NULL,
.passkey_entry = auth_passkey_entry,
.cancel = auth_cancel,
.pairing_confirm = pairing_confirm,
};

static struct bt_conn_auth_info_cb conn_auth_info_callbacks = {
	.pairing_complete = pairing_complete,
	.pairing_failed = pairing_failed
};

int send_button_notification(struct bt_conn *conn, uint8_t value)
{
	if (!notify_enabled) {
		return -EACCES;
	}
    int err = 0;

	return bt_gatt_notify(NULL, &custom_srv.attrs[2],
			      &button_value,
			      sizeof(button_value));

    struct bt_gatt_notify_params params = {0};
    const struct bt_gatt_attr *attr = &custom_srv.attrs[2];

    params.attr = attr;
    params.data = &value;
    params.len = 1;
    params.func = on_sent;

    err = bt_gatt_notify_cb(conn, &params);

    return err;
}


void button_handler(uint32_t button_state, uint32_t has_changed)
{
	uint8_t err;

	if (has_changed)
	{
		switch (has_changed)
		{
			case DK_BTN1_MSK:
				button_value = button_state;
				err = send_button_notification(current_conn, button_value);
        		if (err) {
            		LOG_ERR("Couldn't send notificaton. (err: %d)", err);
        		}
				LOG_INF("Button 1 pressed - %d.", button_value);
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

	LOG_INF("Starting Hello Bluetooth");

	err = dk_leds_init();
	if (err) {
		LOG_ERR("LEDs init failed (err %d)", err);
		return;
	}

	err = dk_buttons_init(button_handler);
    if (err) {
        LOG_ERR("Couldn't init buttons (err %d)", err);
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

	err = bt_le_adv_start(BT_LE_ADV_CONN, ad, ARRAY_SIZE(ad), sd,
			      ARRAY_SIZE(sd));
	if (err) {
		LOG_ERR("Advertising failed to start (err %d)", err);
		return;
	}

	LOG_INF("Advertising successfully started");

}
