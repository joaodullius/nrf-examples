#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/util.h>
#include <nrfx_rtc.h>
#include <nrfx_timer.h>
#include <zephyr/pm/device.h>

#define RTC_INSTANCE 2
#define CC_CHANNEL 0
#define RTC_TIMEOUT_MS 5000
#define RTC_PRESCALER_MS 125
BUILD_ASSERT(RTC_PRESCALER_MS <= 125, "VALUE_MS must be less than or equal to 125");


static nrfx_rtc_t rtc = NRFX_RTC_INSTANCE(RTC_INSTANCE);
static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(DT_ALIAS(led1), gpios);

#ifdef CONFIG_SERIAL
	const struct device *uart_dev0 = DEVICE_DT_GET(DT_NODELABEL(uart0));
	const struct device *uart_dev1 = DEVICE_DT_GET(DT_NODELABEL(uart1));
#endif

void rtc_event_handler(nrfx_rtc_int_type_t int_type)
{
	switch(int_type) {
		case NRFX_RTC_INT_COMPARE0:
			gpio_pin_toggle_dt(&led);
			nrfx_rtc_counter_clear(&rtc);
        	nrfx_rtc_cc_set(&rtc, CC_CHANNEL, RTC_TIMEOUT_MS / RTC_PRESCALER_MS, true);
			break;
		default:
			break;
	}
}

void main(void)
{

	printk("Turn off both UARTs\n");

#ifdef CONFIG_SERIAL	
	pm_device_action_run(uart_dev0, PM_DEVICE_ACTION_SUSPEND);
	pm_device_action_run(uart_dev1, PM_DEVICE_ACTION_SUSPEND);
#endif


	// Configure RTC & LED
    nrfx_rtc_config_t config = NRFX_RTC_DEFAULT_CONFIG;
	printk("RTC_PRESCALER_MS: %d\n", RTC_PRESCALER_MS);
	printk("RTC prescaler in Hz: %d\n", 1000 / RTC_PRESCALER_MS);
	config.prescaler = RTC_FREQ_TO_PRESCALER(1000 / RTC_PRESCALER_MS);
	printk("RTC prescaler: %d\n", config.prescaler);
	
    nrfx_rtc_init(&rtc, &config, rtc_event_handler);

	IRQ_CONNECT(DT_IRQN(DT_NODELABEL(rtc2)),
			DT_IRQ(DT_NODELABEL(rtc2), priority),
			nrfx_isr, nrfx_rtc_2_irq_handler, 0);
	irq_enable(DT_IRQN(DT_NODELABEL(rtc2)));			

	if (!device_is_ready(led.port)) {
        return;
    }

    if (gpio_pin_configure_dt(&led, GPIO_OUTPUT_ACTIVE) < 0) {
        return;
    }

 	nrfx_rtc_tick_disable(&rtc);
    nrfx_rtc_counter_clear(&rtc);
    nrfx_rtc_enable(&rtc);
    nrfx_rtc_cc_set(&rtc, CC_CHANNEL, RTC_TIMEOUT_MS / RTC_PRESCALER_MS, true);
	printk("RTC Compare value: %d\n", RTC_TIMEOUT_MS / RTC_PRESCALER_MS);
	
}