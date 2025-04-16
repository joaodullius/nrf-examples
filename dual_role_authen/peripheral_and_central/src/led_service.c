#include "led_service.h"
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/logging/log.h>
#include <zephyr/drivers/gpio.h>


LOG_MODULE_REGISTER(led_service, LOG_LEVEL_INF);

#define LED0_NODE DT_ALIAS(led0)
static const struct gpio_dt_spec led0 = GPIO_DT_SPEC_GET(LED0_NODE, gpios);

static uint8_t led_value; // Valor inicial do LED

static ssize_t on_led_state_write(struct bt_conn *conn, const struct bt_gatt_attr *attr,
    const void *buf, uint16_t len, uint16_t offset, uint8_t flags) {
    // TODO: Implementar callback de escrita para "Led State"
    led_value = *(uint8_t *)buf; // Atualiza o valor do LED com o valor recebido
    LOG_INF("Led State: %d", led_value);
    gpio_pin_set_dt(&led0, led_value);
    return len;
}

#ifdef CONFIG_BT_SMP
BT_GATT_SERVICE_DEFINE(led_service_svc,
    BT_GATT_PRIMARY_SERVICE(BT_UUID_DECLARE_128(LED_SERVICE_UUID)),
    BT_GATT_CHARACTERISTIC(BT_UUID_DECLARE_128(LED_STATE_CHAR_UUID),
                           BT_GATT_CHRC_WRITE,
                           BT_GATT_PERM_WRITE_ENCRYPT,
                           NULL, on_led_state_write, NULL)
);
#else
BT_GATT_SERVICE_DEFINE(led_service_svc,
    BT_GATT_PRIMARY_SERVICE(BT_UUID_DECLARE_128(LED_SERVICE_UUID)),
    BT_GATT_CHARACTERISTIC(BT_UUID_DECLARE_128(LED_STATE_CHAR_UUID),
                           BT_GATT_CHRC_WRITE,
                           BT_GATT_PERM_WRITE,
                           NULL, on_led_state_write, NULL)
);
#endif // CONFIG_BT_SMP
int led_service_init(void) {
    int ret;

    led_value = 0; // Valor inicial do LED

    if (!gpio_is_ready_dt(&led0)) {
        LOG_ERR("Led Service not initialized: GPIO not ready");
		return -1;
	}

	ret = gpio_pin_configure_dt(&led0, GPIO_OUTPUT_ACTIVE);
	if (ret < 0) {
        LOG_ERR("Led Service not initialized: GPIO pin configure failed");
		return -1;
	}

    gpio_pin_set_dt(&led0, led_value); // Inicializa o LED com o valor inicial

    LOG_INF("Led Service service initialized");
    return 0;
}