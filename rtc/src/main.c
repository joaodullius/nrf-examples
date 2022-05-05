/*
 * Copyright (c) 2021 Joao Dullius
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr.h>
#include <devicetree.h>

#include <nrfx_timer.h>
#include <nrfx_rtc.h>
#include <nrfx_dppi.h>
#include <helpers/nrfx_gppi.h>
#include <nrfx_spim.h>

#include <logging/log.h>
LOG_MODULE_REGISTER(timer_dppi_spim, LOG_LEVEL_INF);

static const nrfx_timer_t m_sample_timer = NRFX_TIMER_INSTANCE(1);

#define LED_0 2
#define LED_1 3

#define COMPARE_COUNTERTIME 3

const nrfx_rtc_t rtc = NRFX_RTC_INSTANCE(0);

static void rtc_handler(nrfx_rtc_int_type_t int_type)
{
	if (int_type == NRFX_RTC_INT_COMPARE0) {
		nrf_gpio_pin_toggle(LED_0);
	} else if (int_type == NRFX_RTC_INT_TICK) {
		nrf_gpio_pin_toggle(LED_1);
	}
}

static void rtc_config(void)
{
	uint32_t err_code;

	nrfx_rtc_config_t config = NRFX_RTC_DEFAULT_CONFIG;
	config.prescaler = 4095;
	err_code = nrfx_rtc_init(&rtc, &config, rtc_handler);
	if (err_code != NRFX_SUCCESS) {
		printk("Failure in setup\n");
		return;
	}

	nrfx_rtc_tick_enable(&rtc, true);
	err_code = nrfx_rtc_cc_set(&rtc, 0, COMPARE_COUNTERTIME * 8, true);
	if (err_code != NRFX_SUCCESS) {
		printk("Failure in setup\n");
		return;
	}

	nrfx_rtc_enable(&rtc);
}

static void manual_isr_setup()
{
	IRQ_DIRECT_CONNECT(RTC0_IRQn, 0, nrfx_rtc_0_irq_handler, 0);
	irq_enable(RTC0_IRQn);
}

void main(void)
{
   	printk("Starting nrfx rtc sample!\n");
	printk("To run in non-secure mode, ");
	printk("you need to add RTC0 to spm.c\n");
	nrf_gpio_cfg_output(LED_0);
	nrf_gpio_cfg_output(LED_1);
	rtc_config();
	manual_isr_setup()
}




