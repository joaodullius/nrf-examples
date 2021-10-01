/*
 * Copyright (c) 2021 Joao Dullius
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr.h>
#include <devicetree.h>

#include <nrfx_timer.h>
#include <nrfx_dppi.h>
#include <helpers/nrfx_gppi.h>
#include <nrfx_spim.h>

#include <logging/log.h>
LOG_MODULE_REGISTER(timer_dppi_spim, LOG_LEVEL_INF);

static const nrfx_timer_t m_sample_timer = NRFX_TIMER_INSTANCE(1);

#define BUF_SIZE 10
uint8_t buffer_tx[BUF_SIZE] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10 };
uint8_t buffer_rx[BUF_SIZE] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };

nrfx_spim_xfer_desc_t transfer = {
	.p_tx_buffer = buffer_tx, ///< Pointer to TX buffer.
	.tx_length = BUF_SIZE, ///< TX buffer length.
	.p_rx_buffer = buffer_rx, ///< Pointer to RX buffer.
	.rx_length = BUF_SIZE ///< RX buffer length.
};

nrfx_spim_t spi_instance = NRFX_SPIM_INSTANCE(2);
nrfx_spim_config_t spi_config =
	NRFX_SPIM_DEFAULT_CONFIG(28, 29, 30, NRFX_SPIM_PIN_NOT_USED);

void timer_handler(nrf_timer_event_t event_type, void* p_context)
{
	LOG_DBG("Timer Handler.");

}

static void timer_init(void)
{
    nrfx_timer_config_t timer_config = NRFX_TIMER_DEFAULT_CONFIG;
	timer_config.bit_width = NRF_TIMER_BIT_WIDTH_32;
    nrfx_err_t err_code = nrfx_timer_init(&m_sample_timer, &timer_config, timer_handler);
    if (err_code != NRFX_SUCCESS) {
        LOG_ERR("nrfx_timer_init error: %08x", err_code);
        return;
    }
    nrfx_timer_extended_compare(&m_sample_timer,
                                NRF_TIMER_CC_CHANNEL0,
                                nrfx_timer_us_to_ticks(&m_sample_timer, 500),
                                NRF_TIMER_SHORT_COMPARE0_CLEAR_MASK,
                                true);
    
    LOG_INF("Timer 1 initialized.");
    nrfx_timer_enable(&m_sample_timer);

	
    IRQ_CONNECT(TIMER1_IRQn, 0,
         nrfx_timer_1_irq_handler, NULL, 0);
}

static void spim_handler(nrfx_spim_evt_t const *p_event, void *p_context)
{
	if (p_event->type == NRFX_SPIM_EVENT_DONE) {
		LOG_INF("SPI transfer finished");
	}
}

static void spi_init(void)
{
	nrfx_err_t err_code;
	
	spi_config.frequency = NRF_SPIM_FREQ_1M;

	IRQ_CONNECT(SPIM2_SPIS2_TWIM2_TWIS2_UARTE2_IRQn, 5, nrfx_isr,
		    nrfx_spim_2_irq_handler, 0);

	err_code = nrfx_spim_init(&spi_instance, &spi_config, spim_handler, NULL);

	if (err_code != NRFX_SUCCESS) {
		LOG_ERR("nrfx_spim_init error: %08x", err_code);
		return;
	}

	err_code = nrfx_spim_xfer(&spi_instance, &transfer, 0);
	if (err_code != NRFX_SUCCESS) {
		LOG_ERR("SPI transfer error: %08x", err_code);
		return;
	}

}

static void dppi_init(void)
{
	uint8_t dppi_ch_1;
	nrfx_err_t err = nrfx_dppi_channel_alloc(&dppi_ch_1);
	if (err != NRFX_SUCCESS) {
		LOG_ERR("Err %d", err);
		return;
	}

	err = nrfx_dppi_channel_enable(dppi_ch_1);
	if (err != NRFX_SUCCESS) {
		LOG_ERR("Err %d", err);
		return;
	}

	nrfx_gppi_channel_endpoints_setup(dppi_ch_1,
			nrf_timer_event_address_get(m_sample_timer.p_reg,
				NRF_TIMER_EVENT_COMPARE0),
			nrf_spim_task_address_get(NRF_SPIM2,
				NRF_SPIM_TASK_START));


	/* Tie it all together */
	nrf_timer_publish_set(m_sample_timer.p_reg, NRF_TIMER_EVENT_COMPARE0, dppi_ch_1);
	nrf_spim_subscribe_set(spi_instance.p_reg, NRF_SPIM_TASK_START, dppi_ch_1);
}

void main(void)
{
    LOG_INF("Timer + DPPI + SPIM Application...");
	timer_init();
	spi_init();
	dppi_init(); 
}




