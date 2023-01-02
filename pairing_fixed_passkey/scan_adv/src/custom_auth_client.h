#include <zephyr/types.h>
#include <stddef.h>
#include <errno.h>
#include <zephyr/kernel.h>

#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/conn.h>
#include <bluetooth/gatt_dm.h>


/** @brief Handles on the connected peer device that are needed to interact with
 * the device.
 */
struct bt_custom_auth_client_handles {

        /** Handle of the LED characteristic, as provided by
	 *  a discovery.
         */
	uint16_t led;
	uint16_t button;
	uint16_t button_ccc;
};

struct bt_custom_auth;

struct bt_custom_auth_cb {
	/** @brief Data received callback.
	 *
	 * The data has been received as a notification of the NUS TX
	 * Characteristic.
	 *
	 * @param[in] nus  NUS Client instance.
	 * @param[in] data Received data.
	 * @param[in] len Length of received data.
	 *
	 * @retval BT_GATT_ITER_CONTINUE To keep notifications enabled.
	 * @retval BT_GATT_ITER_STOP To disable notifications.
	 */
	uint8_t (*received)(struct bt_custom_auth *nus, const uint8_t *data, uint16_t len);

	/** @brief Data sent callback.
	 *
	 * The data has been sent and written to the NUS RX Characteristic.
	 *
	 * @param[in] nus  NUS Client instance.
	 * @param[in] err ATT error code.
	 * @param[in] data Transmitted data.
	 * @param[in] len Length of transmitted data.
	 */
	void (*sent)(struct bt_custom_auth *nus, uint8_t err, const uint8_t *data, uint16_t len);

	/** @brief TX notifications disabled callback.
	 *
	 * TX notifications have been disabled.
	 *
	 * @param[in] nus  NUS Client instance.
	 */
	void (*unsubscribed)(struct bt_custom_auth *nus);
};

/**
 * @brief Custom Auth object.
 *
 * @note
 * This structure is defined here to allow the user to allocate the memory
 * for it in an application-dependent manner.
 * Do not use any of the fields here directly, but use the accessor functions.
 * There are accessors for every field you might need.
 */
struct bt_custom_auth {
	/** Connection object. */
	struct bt_conn *conn;
	/** Internal state. */
	atomic_t state;
    /** Handles on the connected peer device that are needed to interact with the device. */
	struct bt_custom_auth_client_handles handles;

	/** GATT subscribe parameters for BUTTON Characteristic. */
	struct bt_gatt_subscribe_params button_notif_params;

	/** GATT write parameters for LED Characteristic. */
	struct bt_gatt_write_params write_params;

	/** Application callbacks. */
	struct bt_custom_auth_cb cb;
};
