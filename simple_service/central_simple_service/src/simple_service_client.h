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
struct bt_simple_service_client_handles {

        /** Handle of the LED characteristic, as provided by
	 *  a discovery.
         */
	uint16_t led;
	uint16_t button;
	uint16_t button_ccc;
};

struct bt_simple_service;

struct bt_simple_service_cb {
	/** @brief Data received callback.
	 *
	 * The data has been received as a notification of the Read
	 * Characteristic.
	 *
	 * @param[in] simple_service  Simple Service Client instance.
	 * @param[in] data Received data.
	 * @param[in] len Length of received data.
	 *
	 * @retval BT_GATT_ITER_CONTINUE To keep notifications enabled.
	 * @retval BT_GATT_ITER_STOP To disable notifications.
	 */
	uint8_t (*received)(struct bt_simple_service *simple_service, const uint8_t *data, uint16_t len);

	/** @brief Data sent callback.
	 *
	 * The data has been sent and written to the Write Characteristic.
	 *
	 * @param[in] simple_service  Simple Service Client instance.
	 * @param[in] err ATT error code.
	 * @param[in] data Transmitted data.
	 * @param[in] len Length of transmitted data.
	 */
	void (*sent)(struct bt_simple_service *simple_service, uint8_t err, const uint8_t *data, uint16_t len);

	/** @brief TX notifications disabled callback.
	 *
	 * TX notifications have been disabled.
	 *
	 * @param[in] simple_service  Simple Service Client instance.
	 */
	void (*unsubscribed)(struct bt_simple_service *simple_service);
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
struct bt_simple_service {
	/** Connection object. */
	struct bt_conn *conn;
	/** Internal state. */
	atomic_t state;
    /** Handles on the connected peer device that are needed to interact with the device. */
	struct bt_simple_service_client_handles handles;

	/** GATT subscribe parameters for BUTTON Characteristic. */
	struct bt_gatt_subscribe_params button_notif_params;

	/** GATT write parameters for LED Characteristic. */
	struct bt_gatt_write_params write_params;

	/** Application callbacks. */
	struct bt_simple_service_cb cb;
};



/** @brief Assign handles to the NUS Client instance.
 *
 * This function should be called when a link with a peer has been established
 * to associate the link to this instance of the module. This makes it
 * possible to handle several links and associate each link to a particular
 * instance of this module. The GATT attribute handles are provided by the
 * GATT DB discovery module.
 *
 * @param[in] dm Discovery object.
 * @param[in,out] nus NUS Client instance.
 *
 * @retval 0 If the operation was successful.
 * @retval (-ENOTSUP) Special error code used when UUID
 *         of the service does not match the expected UUID.
 * @retval Otherwise, a negative error code is returned.
 */
int bt_simple_service_handles_assign(struct bt_gatt_dm *dm,
			  struct bt_simple_service *simple_service_c);




/** @brief Request the peer to start sending notifications for the TX
 *	   Characteristic.
 *
 * This function enables notifications for the NUS TX Characteristic at the peer
 * by writing to the CCC descriptor of the NUS TX Characteristic.
 *
 * @param[in,out] nus NUS Client instance.
 *
 * @retval 0 If the operation was successful.
 *           Otherwise, a negative error code is returned.
 */
int bt_simple_service_subscribe_receive(struct bt_simple_service *simple_service_c);

int bt_simple_service_set_led(struct bt_simple_service *simple_service_c, const uint8_t data);