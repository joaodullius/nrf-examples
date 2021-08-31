/*
 * Copyright (c) 2016 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr.h>
#include <device.h>
#include <devicetree.h>
#include <drivers/gpio.h>
#include <timing/timing.h>
#include <nrfx_timer.h>

/* The devicetree node identifier for the "led0" alias. */
#define LED0_NODE DT_ALIAS(led0)
#define LED0	DT_GPIO_LABEL(LED0_NODE, gpios)
#define PIN_LED0	DT_GPIO_PIN(LED0_NODE, gpios)
#define FLAGS_LED0	DT_GPIO_FLAGS(LED0_NODE, gpios)

static bool led0_is_on = true;
static const struct device *dev_led0;

static const nrfx_timer_t m_sample_timer = NRFX_TIMER_INSTANCE(1);

void timer_handler(nrf_timer_event_t event_type, void* p_context)
{
   	printk("timer_handler()....\n");
    gpio_pin_set(dev_led0, PIN_LED0, (int)led0_is_on);
	led0_is_on = !led0_is_on;
}

static void timer_init(void)
{
    printk("timer_init function...\n");
    nrfx_err_t err_code;

    nrfx_timer_config_t timer_config = NRFX_TIMER_DEFAULT_CONFIG;
	timer_config.bit_width = NRF_TIMER_BIT_WIDTH_32;
    err_code = nrfx_timer_init(&m_sample_timer, &timer_config, timer_handler);
    if (err_code != NRFX_SUCCESS) {
        printk("nrfx_timer_init error: %08x\n", err_code);
        return;
    }

    printk("nrfx_timer_init\n");
	
    nrfx_timer_extended_compare(&m_sample_timer,
                                NRF_TIMER_CC_CHANNEL0,
                                nrfx_timer_ms_to_ticks(&m_sample_timer, 500),
                                NRF_TIMER_SHORT_COMPARE0_CLEAR_MASK,
                                false);
    
    printk("nrfx_timer_extended_compare\n");

    nrfx_timer_resume(&m_sample_timer);
    printk("nrfx_timer_resume\n");
}

void main(void)
{


    printk("Starting Blinky + Timer Application...\n");
	int ret;

	dev_led0 = device_get_binding(LED0);
	if (dev_led0 == NULL) {
		return;
	}

	ret = gpio_pin_configure(dev_led0, PIN_LED0, GPIO_OUTPUT_ACTIVE | FLAGS_LED0);
	if (ret < 0) {
		return;
	}

	timer_init();

    printk("Timer Initialized...\n");

    while(1){
        k_sleep(K_USEC(250));
    }


}




