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

	/** GATT write parameters for LED Characteristic. */
	struct bt_gatt_write_params write_params;
};
