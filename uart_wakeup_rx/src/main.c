#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/util.h>
#include <nrfx_rtc.h>
#include <nrfx_timer.h>
#include <zephyr/pm/device.h>

static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(DT_ALIAS(led1), gpios);
static const struct gpio_dt_spec button1 = GPIO_DT_SPEC_GET(DT_ALIAS(sw1), gpios);

const struct device *uart_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_console));

// Referente the UART RX pin on nRF52840DK
#define RX_DEVICE DT_NODELABEL(gpio0)
#define RX_PIN 8
const struct device *gpio_dev;


static void uart0_set_enable(bool enable)
{
	const struct device *uart_dev = DEVICE_DT_GET(DT_NODELABEL(uart0));

	if (!device_is_ready(uart_dev)) {
		return;
	}

	gpio_pin_interrupt_configure(gpio_dev, RX_PIN, enable? GPIO_INT_DISABLE : GPIO_INT_EDGE_TO_ACTIVE);
	gpio_pin_set_dt(&led, enable ? 1 : 0);
	pm_device_action_run(uart_dev, enable ? PM_DEVICE_ACTION_RESUME : PM_DEVICE_ACTION_SUSPEND);
}

/* Button handler functions */
static void rx_handler(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
    uart0_set_enable(true);
}
static struct gpio_callback rx_cb;

static void button1_handler(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
    uart0_set_enable(false);
}
static struct gpio_callback button1_cb;

void main(void)
{

	printk("Initializing...\n");
	/*
	gpio_pin_configure_dt(&button0, GPIO_INPUT);
	gpio_pin_interrupt_configure_dt(&button0, GPIO_INT_EDGE_TO_ACTIVE);
	gpio_init_callback(&button0_cb, button0_handler, BIT(button0.pin));
	gpio_add_callback(button0.port, &button0_cb);
	*/

	gpio_pin_configure_dt(&button1, GPIO_INPUT);
	gpio_pin_interrupt_configure_dt(&button1, GPIO_INT_EDGE_TO_ACTIVE);
	gpio_init_callback(&button1_cb, button1_handler, BIT(button1.pin));
	gpio_add_callback(button1.port, &button1_cb);


	gpio_dev = DEVICE_DT_GET(RX_DEVICE);
	//gpio_pin_configure(gpio_dev, RX_PIN, GPIO_INPUT | GPIO_PULL_UP);
	gpio_pin_interrupt_configure(gpio_dev, RX_PIN, GPIO_INT_EDGE_TO_ACTIVE);
	gpio_init_callback(&rx_cb, rx_handler, BIT(RX_PIN));
	gpio_add_callback(gpio_dev, &rx_cb);

	uart0_set_enable(true);

	if (!device_is_ready(led.port)) {
		return;
	}

	if (gpio_pin_configure_dt(&led, GPIO_OUTPUT_ACTIVE) < 0) {
		return;
	}

	while (1) {
		printk("Printing...\n");
		k_sleep(K_SECONDS(1));
	}

}