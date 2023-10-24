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

		if ((c == '\n' || c == '\r') && rx0_buf_pos > 0) {
			/* terminate string */
			rx0_buf[rx0_buf_pos] = '\0';

			/* if queue is full, message is silently dropped */
			k_msgq_put(&uart0_msgq, &rx0_buf, K_NO_WAIT);

			/* reset the buffer (it was copied to the msgq) */
			rx0_buf_pos = 0;
		} else if (rx0_buf_pos < (sizeof(rx0_buf) - 1)) {
			rx0_buf[rx0_buf_pos++] = c;
		}
		/* else: characters beyond buffer size are dropped */
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

		if ((c == '\n' || c == '\r') && rx1_buf_pos > 0) {
			/* terminate string */
			rx0_buf[rx1_buf_pos] = '\0';

			/* if queue is full, message is silently dropped */
			k_msgq_put(&uart1_msgq, &rx1_buf, K_NO_WAIT);

			/* reset the buffer (it was copied to the msgq) */
			rx1_buf_pos = 0;
		} else if (rx1_buf_pos < (sizeof(rx1_buf) - 1)) {
			rx1_buf[rx1_buf_pos++] = c;
		}
		/* else: characters beyond buffer size are dropped */
	}
}

void print_uart(const struct device * dev, char *buf)
{
	int msg_len = strlen(buf);

	for (int i = 0; i < msg_len; i++) {
		uart_poll_out(dev, buf[i]);
	}
}

/*
 * Print a null-terminated string character by character to the UART interface
 */
#define PRINT_THREAD_STACK 2048
void print_uart0_fn(void){
	while (1)
	{
		/* indefinitely wait for input from the user */
		/* Uart_0 waits Uart_1 to print*/
		while (k_msgq_get(&uart1_msgq, &tx1_buf, K_FOREVER) == 0) {
			print_uart(uart0_dev, "From Uart1: ");
			print_uart(uart0_dev, tx1_buf);
			print_uart(uart0_dev, "\r\n");
		}
	}
}

void print_uart1_fn(void){
	while (1)
	{
		/* indefinitely wait for input from the user */
		/* Uart_1 waits Uart_0 to print*/
		while (k_msgq_get(&uart0_msgq, &tx0_buf, K_FOREVER) == 0) {
			print_uart(uart1_dev, "From Uart0: ");
			print_uart(uart1_dev, tx0_buf);
			print_uart(uart1_dev, "\r\n");
		}
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

K_THREAD_DEFINE(print_uart0_thread, PRINT_THREAD_STACK,
				print_uart0_fn, NULL, NULL, NULL,
				K_LOWEST_APPLICATION_THREAD_PRIO, 0, 0);

K_THREAD_DEFINE(print_uart1_thread, PRINT_THREAD_STACK,
				print_uart1_fn, NULL, NULL, NULL,
				K_LOWEST_APPLICATION_THREAD_PRIO, 0, 0);