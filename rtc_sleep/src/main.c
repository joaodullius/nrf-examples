#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <nrfx_rtc.h>
#include <nrfx_timer.h>

#define RTC_INSTANCE 2
#define CC_CHANNEL 0
#define RTC_TIMEOUT_MS 5000
#define RTC_PRESCALER_MS 125

static nrfx_rtc_t rtc = NRFX_RTC_INSTANCE(RTC_INSTANCE);
static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(DT_ALIAS(led1), gpios);

void rtc_event_handler(nrfx_rtc_int_type_t int_type)
{
	switch(int_type) {
		case NRFX_RTC_INT_COMPARE0:
			printk("RTC compare event occurred\n");
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
    printk("Hello World!\n");

    nrfx_rtc_config_t config = NRFX_RTC_DEFAULT_CONFIG;
	config.prescaler = RTC_FREQ_TO_PRESCALER(1000 / RTC_PRESCALER_MS);
	
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
}