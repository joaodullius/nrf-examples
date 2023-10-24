/*
 * Copyright (c) 2022 Libre Solar Technologies GmbH
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <hal/nrf_uarte.h>

#include <string.h>

/* change this to any other UART peripheral if desired */


#define MSG_SIZE 32

/* queue to store up to 10 messages (aligned to 4-byte boundary) */
K_MSGQ_DEFINE(uart0_msgq, MSG_SIZE, 10, 4);
K_MSGQ_DEFINE(uart1_msgq, MSG_SIZE, 10, 4);

static const struct device *uart0_dev = DEVICE_DT_GET(DT_NODELABEL(uart0));
static const struct device *uart1_dev = DEVICE_DT_GET(DT_NODELABEL(uart1));

/* receive buffer used in UART ISR callback */
static char rx0_buf[MSG_SIZE], rx1_buf[MSG_SIZE];
static char tx0_buf[MSG_SIZE], tx1_buf[MSG_SIZE];
static int rx0_buf_pos, rx1_buf_pos;


/*
 * Read characters from UART until line end is detected. Afterwards push the
 * data to the message queue.
 */
void uart0_cb(const struct device *dev, void *user_data)
{
	uint8_t c;

	if (!uart_irq_update(uart0_dev)) {
		return;
	}

	while (uart_irq_rx_ready(uart0_dev)) {

		uart_fifo_read(uart0_dev, &c, 1);

		/* send every byte received on &c to uart1*/
		uart_poll_out(uart1_dev, c);
	}
}

void uart1_cb(const struct device *dev, void *user_data)
{
	uint8_t c;

	if (!uart_irq_update(uart1_dev)) {
		return;
	}

	while (uart_irq_rx_ready(uart1_dev)) {

		uart_fifo_read(uart1_dev, &c, 1);

		/* send every byte received on &c to uart0*/
		uart_poll_out(uart0_dev, c);
	}
}

void print_uart(const struct device * dev, char *buf)
{
	int msg_len = strlen(buf);

	for (int i = 0; i < msg_len; i++) {
		uart_poll_out(dev, buf[i]);
	}
}

struct uarte_nrfx_config {
	NRF_UARTE_Type *uarte_regs; /* Instance address */
	uint32_t flags;
	bool disable_rx;
	const struct pinctrl_dev_config *pcfg;
#ifdef UARTE_ANY_ASYNC
	nrfx_timer_t timer;
#endif
};

static inline NRF_UARTE_Type *get_uarte_instance(const struct device *dev)
{
	const struct uarte_nrfx_config *config = dev->config;

	return config->uarte_regs;
}


void main(void)
{

	if (uart0_dev == NULL || !device_is_ready(uart0_dev)) {
		printk("UART_0 device not found!");
		return;
	}	

	if (uart1_dev == NULL || !device_is_ready(uart1_dev)) {
		printk("UART_1 device not found!");
		return;
	}	

	NRF_UARTE_Type *uarte1 = get_uarte_instance(uart1_dev);
	printk("Uart1 Baudrate config: %x\n", uarte1->BAUDRATE);

	//config uart1 baudrate to 100kbps
	uarte1->BAUDRATE = 0x01999D1C;  //100kbps
	printk("Uart1 Baudrate new: %x\n", uarte1->BAUDRATE);
		
	/* configure interrupt and callback to receive data */
	uart_irq_callback_user_data_set(uart0_dev, uart0_cb, NULL);
	uart_irq_rx_enable(uart0_dev);

		/* configure interrupt and callback to receive data */
	uart_irq_callback_user_data_set(uart1_dev, uart1_cb, NULL);
	uart_irq_rx_enable(uart1_dev);

	print_uart(uart0_dev, "Hello! This is Uart_0\r\n");
	print_uart(uart0_dev, "Tell me something and press enter:\r\n");

	print_uart(uart1_dev, "Hello! This is Uart_1\r\n");
	print_uart(uart1_dev, "Tell me something and press enter:\r\n");

}