/*
 * Copyright (c) 2020 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

/** @file
 *  @brief Peripheral Heart Rate over LE Coded PHY sample
 */

#include <zephyr/types.h>
#include <stddef.h>
#include <string.h>
#include <errno.h>
#include <sys/printk.h>
#include <zephyr.h>

#include <bluetooth/bluetooth.h>
#include <bluetooth/conn.h>
#include <bluetooth/uuid.h>
#include <bluetooth/gatt.h>
#include <bluetooth/services/bas.h>
#include <bluetooth/services/hrs.h>

static struct k_work start_coded_advertising_worker;
static struct k_work start_regular_advertising_worker;

static struct bt_le_ext_adv *adv_coded;
static struct bt_le_ext_adv *adv_regular;

static const struct bt_data ad_coded[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
	BT_DATA_BYTES(BT_DATA_UUID16_ALL, 0x0d, 0x18, 0x0f, 0x18, 0x0a, 0x18),
};

static const struct bt_data ad_regular[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
	BT_DATA_BYTES(BT_DATA_UUID16_ALL,
		      BT_UUID_16_ENCODE(BT_UUID_HRS_VAL),
		      BT_UUID_16_ENCODE(BT_UUID_BAS_VAL),
		      BT_UUID_16_ENCODE(BT_UUID_DIS_VAL))
};

static void connected(struct bt_conn *conn, uint8_t conn_err)
{
	int err;
	struct bt_conn_info info;
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	if (conn_err) {
		printk("Connection failed (err %d)\n", conn_err);
		return;
	}

	err = bt_conn_get_info(conn, &info);

	if (err) {
		printk("Failed to get connection info\n");
	} else {
		const struct bt_conn_le_phy_info *phy_info;
		phy_info = info.le.phy;

		printk("Connected: %s, tx_phy %u, rx_phy %u\n",
		       addr, phy_info->tx_phy, phy_info->rx_phy);
	}
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
	int err;
	struct bt_conn_info info;
	char addr[BT_ADDR_LE_STR_LEN];
	printk("Disconnected (reason 0x%02x)\n", reason);

	err = bt_conn_get_info(conn, &info);

	if (err) {
		printk("Failed to get connection info\n");
	} else {
		const struct bt_conn_le_phy_info *phy_info;
		phy_info = info.le.phy;

		printk("Disconnect: %s, tx_phy %u, rx_phy %u\n",
	       addr, phy_info->tx_phy, phy_info->rx_phy);

		if(phy_info->tx_phy==BT_CONN_LE_PHY_OPT_CODED_S8) {
			k_work_submit(&start_coded_advertising_worker);
		} else {
			k_work_submit(&start_regular_advertising_worker);
		}
	}
}

static struct bt_conn_cb conn_callbacks = {
	.connected = connected,
	.disconnected = disconnected,
};

static int create_advertising_coded(void)
{
	int err;
	struct bt_le_adv_param param =
		BT_LE_ADV_PARAM_INIT(BT_LE_ADV_OPT_CONNECTABLE |
				     BT_LE_ADV_OPT_EXT_ADV |
				     BT_LE_ADV_OPT_CODED,
				     BT_GAP_ADV_FAST_INT_MIN_2,
				     BT_GAP_ADV_FAST_INT_MAX_2,
				     NULL);

	err = bt_le_ext_adv_create(&param, NULL, &adv_coded);
	if (err) {
		printk("Failed to create advertiser coded set (%d)\n", err);
		return err;
	}

	printk("Created adv_coded: %p\n", adv_coded);

	err = bt_le_ext_adv_set_data(adv_coded, ad_coded, ARRAY_SIZE(ad_coded), NULL, 0);
	if (err) {
		printk("Failed to set advertising coded data (%d)\n", err);
		return err;
	}

	return 0;
}

static int create_advertising_regular(void)
{
	int err;
	struct bt_le_adv_param param =
		BT_LE_ADV_PARAM_INIT(BT_LE_ADV_OPT_CONNECTABLE |
					 BT_LE_ADV_OPT_USE_NAME,
				     BT_GAP_ADV_FAST_INT_MIN_2,
				     BT_GAP_ADV_FAST_INT_MAX_2,
				     NULL);

	err = bt_le_ext_adv_create(&param, NULL, &adv_regular);
	if (err) {
		printk("Failed to create advertiser regular set (%d)\n", err);
		return err;
	}

	printk("Created adv_regular: %p\n", adv_regular);

	err = bt_le_ext_adv_set_data(adv_regular, ad_regular, ARRAY_SIZE(ad_regular), NULL, 0);
	if (err) {
		printk("Failed to set advertising data (%d)\n", err);
		return err;
	}

	return 0;
}

static void start_advertising_coded(struct k_work *item)
{
	int err;

	err = bt_le_ext_adv_start(adv_coded, NULL);
	if (err) {
		printk("Failed to start advertising set (%d)\n", err);
		return;
	}

	printk("Advertiser Coded %p set started\n", adv_coded);
}

static void start_advertising_regular(struct k_work *item)
{
	int err;

	err = bt_le_ext_adv_start(adv_regular, NULL);
	if (err) {
		printk("Failed to start advertising set (%d)\n", err);
		return;
	}

	printk("Advertiser Regular %p set started\n", adv_regular);
}

static void bt_ready(void)
{
	int err = 0;

	printk("Bluetooth initialized\n");

	k_work_init(&start_regular_advertising_worker, start_advertising_regular);
	k_work_init(&start_coded_advertising_worker, start_advertising_coded);
	


	err = create_advertising_regular();
	if (err) {
		printk("Advertising failed to create (err %d)\n", err);
		return;
	}

	err = create_advertising_coded();
	if (err) {
		printk("Advertising Coded failed to create (err %d)\n", err);
		return;
	}

	k_work_submit(&start_regular_advertising_worker);
	k_work_submit(&start_coded_advertising_worker);
}

static void bas_notify(void)
{
	uint8_t battery_level = bt_bas_get_battery_level();

	battery_level--;

	if (!battery_level) {
		battery_level = 100U;
	}

	bt_bas_set_battery_level(battery_level);
}

static void hrs_notify(void)
{
	static uint8_t heartrate = 90U;

	/* Heartrate measurements simulation */
	heartrate++;
	if (heartrate == 160U) {
		heartrate = 90U;
	}

	bt_hrs_notify(heartrate);
}

void main(void)
{
	int err;

	err = bt_enable(NULL);
	if (err) {
		printk("Bluetooth init failed (err %d)\n", err);
		return;
	}

	bt_ready();

	bt_conn_cb_register(&conn_callbacks);

	/* Implement notification. At the moment there is no suitable way
	 * of starting delayed work so we do it here
	 */
	while (1) {
		k_sleep(K_SECONDS(1));

		/* Heartrate measurements simulation */
		hrs_notify();

		/* Battery level simulation */
		bas_notify();
	}
}
