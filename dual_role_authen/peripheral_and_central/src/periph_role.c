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
 LOG_MODULE_REGISTER(periph_role, LOG_LEVEL_INF);
 
 #include <zephyr/bluetooth/bluetooth.h>
 #include <zephyr/bluetooth/conn.h>
 #include <zephyr/bluetooth/gatt.h>
 #include <zephyr/bluetooth/hci.h>
 
 #include "led_service.h"
 
 
 // PERIPHERAL SIDE OF THE DUAL ROLE AUTHENTICATION EXAMPLE
 #define DEVICE_NAME CONFIG_BT_DEVICE_NAME
 #define DEVICE_NAME_LEN (sizeof(DEVICE_NAME) - 1)
 
#ifdef CONFIG_BT_SMP
#define FIXED_PASSKEY 123456
#endif // CONFIG_BT_SMP
 
 
 static const struct bt_data ad[] = {
    BT_DATA_BYTES(BT_DATA_FLAGS, BT_LE_AD_NO_BREDR),
#ifndef CONFIG_BT_CENTRAL
    BT_DATA_BYTES(BT_DATA_UUID128_ALL, LED_SERVICE_UUID),
#endif // CONFIG_BT_CENTRAL

 };
 
 static const struct bt_data sd[] = {
     BT_DATA(BT_DATA_NAME_COMPLETE, DEVICE_NAME, DEVICE_NAME_LEN),
 };
 
 
 #ifdef CONFIG_BT_SMP
 static void auth_passkey_display(struct bt_conn *conn, unsigned int passkey)
 {
     char addr[BT_ADDR_LE_STR_LEN];
     bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
     LOG_INF("Passkey for %s: %06u\n", addr, passkey);
 }
 
 static void auth_cancel(struct bt_conn *conn)
 {
     char addr[BT_ADDR_LE_STR_LEN];
     bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
     LOG_INF("Pairing cancelled: %s\n", addr);
 }
 
 static struct bt_conn_auth_cb conn_auth_callbacks = {
     .passkey_display = auth_passkey_display,
     .cancel = auth_cancel,
 };
 #endif
 
 static void connected(struct bt_conn *conn, uint8_t err)
 {
     struct bt_conn_info info; 
     char addr[BT_ADDR_LE_STR_LEN];
 
     if (err) {
         LOG_INF("Connection failed (err %u)", err);
         return;
     }
     else if(bt_conn_get_info(conn, &info)) {
         LOG_INF("Could not parse connection info");
     }
     else {
         bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
         
         LOG_INF("Connection established!");
         LOG_INF("Connected to: %s", addr);
         LOG_INF("Role: %u", info.role);
         LOG_INF("Connection interval: %u", info.le.interval);
         LOG_INF("Slave latency: %u", info.le.latency);
         LOG_INF("Connection supervisory timeout: %u", info.le.timeout);
     }   
 }
 
 
 static void disconnected(struct bt_conn *conn, uint8_t reason)
 {
     printk("Disconnected (reason %u)", reason);
     // Additional disconnection handling code
 }

 #ifdef CONFIG_BT_SMP
 
 static void on_security_changed(struct bt_conn *conn, bt_security_t level,
     enum bt_security_err err)
 {
     char addr[BT_ADDR_LE_STR_LEN];
 
     bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
 
     if (!err) {
     LOG_INF("Security changed: %s level %u\n", addr, level);
     } else {
     LOG_INF("Security failed: %s level %u err %d\n", addr, level,
     err);
     }
 }
 #endif // CONFIG_BT_SMP

 static struct bt_conn_cb conn_callbacks = {
     .connected = connected,
     .disconnected = disconnected,
#ifdef CONFIG_BT_SMP
     .security_changed = on_security_changed,
#endif // CONFIG_BT_SMP
 };
 
 void periph_bt_ready(int err)
 {
     char addr_s[BT_ADDR_LE_STR_LEN];
     bt_addr_le_t addr = {0};
     size_t count = 1;
 
     if (err) {
         printk("Bluetooth init failed (err %d)", err);
         return;
     }
 
     printk("Bluetooth initialized");
 
     /* Start advertising */
     err =  bt_le_adv_start(BT_LE_ADV_CONN, ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));
     if (err) {
         printk("Advertising failed to start (err %d)", err);
         return;
     }
 
 
     /* For connectable advertising you would use
      * bt_le_oob_get_local().  For non-connectable non-identity
      * advertising an non-resolvable private address is used;
      * there is no API to retrieve that.
      */
 
     bt_id_get(&addr, &count);
     bt_addr_le_to_str(&addr, addr_s, sizeof(addr_s));
 
     printk("Beacon started, advertising as %s", addr_s);
 }
 
 int periph_role_init(void)
 {
 
     LOG_INF("Initialing Peripheral Mode");
 
     /* Initialize the Bluetooth Subsystem */
 
     led_service_init();
 
     bt_conn_cb_register(&conn_callbacks);

#ifdef CONFIG_BT_SMP
     bt_conn_auth_cb_register(&conn_auth_callbacks);
     bt_passkey_set(FIXED_PASSKEY); // Set the passkey for pairing
#endif // CONFIG_BT_SMP

     return 0;
 }
 