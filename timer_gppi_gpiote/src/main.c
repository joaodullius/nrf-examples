/*
 * Copyright (c) 2021 Joao Dulliuswest 
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr.h>
#include <device.h>
#include <devicetree.h>
#include <drivers/gpio.h>
#include <timing/timing.h>

#include <nrfx_timer.h>
#include <nrfx_dppi.h>
#include <nrfx_gpiote.h>
#include <helpers/nrfx_gppi.h>

/* The devicetree node identifier for the "led0" alias. */
#define LED0_NODE DT_ALIAS(led0)
#define LED0	DT_GPIO_LABEL(LED0_NODE, gpios)
#define PIN_LED0	DT_GPIO_PIN(LED0_NODE, gpios)
#define FLAGS_LED0	DT_GPIO_FLAGS(LED0_NODE, gpios)

#define LED_GPIOTE true

#if !defined(LED_GPIOTE)
	static bool led0_is_on = true;
	static const struct device *dev_led0;
#endif

static const nrfx_timer_t m_sample_timer = NRFX_TIMER_INSTANCE(1);

void timer_handler(nrf_timer_event_t event_type, void* p_context)
{
#if !defined(LED_GPIOTE)
    gpio_pin_set(dev_led0, PIN_LED0, (int)led0_is_on);
	led0_is_on = !led0_is_on;
#endif
}

static void timer_init(void)
{
    nrfx_timer_config_t timer_config = NRFX_TIMER_DEFAULT_CONFIG;
	timer_config.bit_width = NRF_TIMER_BIT_WIDTH_32;
    nrfx_err_t err_code = nrfx_timer_init(&m_sample_timer, &timer_config, timer_handler);
    if (err_code != NRFX_SUCCESS) {
        printk("nrfx_timer_init error: %08x\n", err_code);
        return;
    }
    nrfx_timer_extended_compare(&m_sample_timer,
                                NRF_TIMER_CC_CHANNEL0,
                                nrfx_timer_ms_to_ticks(&m_sample_timer, 500),
                                NRF_TIMER_SHORT_COMPARE0_CLEAR_MASK,
                                true);
    
    printk("Timer 1 initialized.\n");
    nrfx_timer_enable(&m_sample_timer);

	
    IRQ_CONNECT(TIMER1_IRQn, 0,
         nrfx_timer_1_irq_handler, NULL, 0);
}

#if !defined(LED_GPIOTE)
static void gpio_init(void)
{
	dev_led0 = device_get_binding(LED0);
	if (dev_led0 == NULL) {
		return;
	}

	int ret = gpio_pin_configure(dev_led0, PIN_LED0, GPIO_OUTPUT_ACTIVE | FLAGS_LED0);
	if (ret < 0) {
		return;
	}
}
#endif

#if defined(LED_GPIOTE)
static void gpiote_init(void)
{
	nrfx_gpiote_out_config_t led_pin_cfg = {0};

	led_pin_cfg.action = NRF_GPIOTE_POLARITY_TOGGLE;
	led_pin_cfg.init_state = NRF_GPIOTE_INITIAL_VALUE_HIGH;
	led_pin_cfg.task_pin = true;

	nrfx_err_t err = nrfx_gpiote_init(NRFX_GPIOTE_DEFAULT_CONFIG_IRQ_PRIORITY);
	if (err != NRFX_SUCCESS) {
		printk("gpiote1 err: %x", err);
		return;
	}

	err = nrfx_gpiote_out_init(PIN_LED0, &led_pin_cfg);
	if (err != NRFX_SUCCESS) {
		printk("gpiote1 err: %x", err);
		return;
	}

	nrfx_gpiote_out_task_enable(PIN_LED0);
}

static void dppi_init(void)
{
	uint8_t dppi_ch_1;
	nrfx_err_t err = nrfx_dppi_channel_alloc(&dppi_ch_1);
	if (err != NRFX_SUCCESS) {
		printk("Err %d\n", err);
		return;
	}

	err = nrfx_dppi_channel_enable(dppi_ch_1);
	if (err != NRFX_SUCCESS) {
		printk("Err %d\n", err);
		return;
	}

	/* Tie it all together */
	//nrf_timer_publish_set(m_sample_timer.p_reg, NRF_TIMER_EVENT_COMPARE0, dppi_ch_1);
	//nrf_gpiote_subscribe_set(led_pin.p_reg, NRF_GPIOTE_TASK_OUT_0, dppi_ch_1);


	nrfx_gppi_channel_endpoints_setup(dppi_ch_1,
			nrf_timer_event_address_get(m_sample_timer.p_reg,
				NRF_TIMER_EVENT_COMPARE0),
			nrf_gpiote_task_address_get(NRF_GPIOTE,
				nrfx_gpiote_in_event_get(PIN_LED0)));

}
#endif

void main(void)
{
    printk("Starting Fancy Blinker Application...\n");
	timer_init();

#if defined(LED_GPIOTE)
	gpiote_init();
	dppi_init();
	printk("Timer + DPPI + GPIOTE Mode\n");
#else
	gpio_init();
	printk("Timer + GPIO Mode\n");
#endif    

}




