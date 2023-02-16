/*
 * Copyright (c) 2020 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr/kernel.h>
#include <stdio.h>
#include <modem/lte_lc.h>
#include <zephyr/net/socket.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>

#define UDP_IP_HEADER_SIZE 28

LOG_MODULE_REGISTER(gnss_udp, LOG_LEVEL_INF);


#define BT1_NODE DT_ALIAS(sw0)

static const struct gpio_dt_spec buttons[] = {
	GPIO_DT_SPEC_GET_OR(BT1_NODE, gpios,
						{0})};
static struct gpio_callback button_cb_data[4];

#define SERVER_HOSTNAME "nordicecho.westeurope.cloudapp.azure.com"
#define SERVER_PORT "2444"

#define MESSAGE_SIZE 256 
#define MESSAGE_TO_SEND "Hello from GNSS UDP"
#define SSTRLEN(s) (sizeof(s) - 1)

static uint8_t recv_buf[MESSAGE_SIZE];

static int sock;
static struct sockaddr_storage server;
static struct k_work_delayable server_transmission_work;

static volatile enum state_type { LTE_STATE_ON,
								  LTE_STATE_OFFLINE,
								  LTE_STATE_BUSY } LTE_Connection_Current_State;
static volatile enum state_type LTE_Connection_Target_State;

void button_pressed(const struct device *dev, struct gpio_callback *cb,
					uint32_t pins)
{
	int val;

	LOG_INF("Button pressed at %" PRIu32, k_cycle_get_32());

	val = gpio_pin_get_dt(&buttons[0]);
	if (val == 1 && LTE_Connection_Current_State == LTE_STATE_ON) // button1 pressed
	{
		LOG_INF("Send UDP package!");
		k_work_reschedule(&server_transmission_work, K_NO_WAIT);
	}

}

void button_init(void)
{
	int ret;
	for (size_t i = 0; i < ARRAY_SIZE(buttons); i++)
	{
		ret = gpio_pin_configure_dt(&buttons[i], GPIO_INPUT);
		if (ret != 0)
		{
			LOG_ERR("Error %d: failed to configure %s pin %d",
				   ret, buttons[i].port->name, buttons[i].pin);
			return;
		}

		ret = gpio_pin_interrupt_configure_dt(&buttons[i],
											  GPIO_INT_EDGE_TO_ACTIVE);
		if (ret != 0)
		{
			LOG_ERR("Error %d: failed to configure interrupt on %s pin %d",
				   ret, buttons[i].port->name, buttons[i].pin);
			return;
		}

		gpio_init_callback(&button_cb_data[i], button_pressed, BIT(buttons[i].pin));
		gpio_add_callback(buttons[i].port, &button_cb_data[i]);
		LOG_INF("Set up button at %s pin %d", buttons[i].port->name, buttons[i].pin);
	}
}

static int server_resolve(void)
{
	/* STEP 6.1 - Call getaddrinfo() to get the IP address of the echo server */
	int err;
	struct addrinfo *result;
	struct addrinfo hints = {
		.ai_family = AF_INET,
		.ai_socktype = SOCK_DGRAM
	};
	
	err = getaddrinfo(SERVER_HOSTNAME, SERVER_PORT, &hints, &result);
	if (err != 0) {
		LOG_INF("ERROR: getaddrinfo failed %d", err);
		return -EIO;
	}

	if (result == NULL) {
		LOG_INF("ERROR: Address not found");
		return -ENOENT;
	} 	

	/* STEP 6.2 - Retrieve the relevant information from the result structure*/
	struct sockaddr_in *server4 = ((struct sockaddr_in *)&server);

	server4->sin_addr.s_addr =
		((struct sockaddr_in *)result->ai_addr)->sin_addr.s_addr;
	server4->sin_family = AF_INET;
	server4->sin_port = ((struct sockaddr_in *)result->ai_addr)->sin_port;
	
	/* STEP 6.3 - Convert the address into a string and print it */
	char ipv4_addr[NET_IPV4_ADDR_LEN];
	inet_ntop(AF_INET, &server4->sin_addr.s_addr, ipv4_addr,
		  sizeof(ipv4_addr));
	LOG_INF("IPv4 Address found %s", ipv4_addr);
	
	/* STEP 6.4 - Free the memory allocated for result */
	freeaddrinfo(result);

	return 0;
}

static void server_disconnect(void)
{
	(void)close(sock);
}
static int server_connect(void)
{
	int err;

	sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (sock < 0)
	{
		LOG_ERR("Failed to create UDP socket: %d", errno);
		err = -errno;
		goto error;
	}

	err = connect(sock, (struct sockaddr *)&server,
				  sizeof(struct sockaddr_in));
	if (err < 0)
	{
		LOG_ERR("Connect failed : %d", errno);
		goto error;
	}
	LOG_INF("Connected to %s", SERVER_HOSTNAME);

	return 0;

error:
	server_disconnect();

	return err;
}

#if defined(CONFIG_NRF_MODEM_LIB)
static void lte_handler(const struct lte_lc_evt *const evt)
{
	switch (evt->type)
	{
	case LTE_LC_EVT_NW_REG_STATUS:
		LOG_INF("Network registration status: %d",
			   evt->nw_reg_status);

		if ((evt->nw_reg_status != LTE_LC_NW_REG_REGISTERED_HOME) &&
			(evt->nw_reg_status != LTE_LC_NW_REG_REGISTERED_ROAMING))
		{
			if (evt->nw_reg_status == 0)
			{
				LTE_Connection_Current_State = LTE_STATE_OFFLINE;
				LOG_ERR("LTE OFFLINE!");
				break;
			}
			break;
		}

		LOG_INF("Network registration status: %s",
			   evt->nw_reg_status == LTE_LC_NW_REG_REGISTERED_HOME ? "Connected - home network" : "Connected - roaming");
		LTE_Connection_Current_State = LTE_STATE_ON;
		break;
	case LTE_LC_EVT_PSM_UPDATE:
		LOG_INF("PSM parameter update: TAU: %d, Active time: %d",
			   evt->psm_cfg.tau, evt->psm_cfg.active_time);
		break;
	case LTE_LC_EVT_EDRX_UPDATE:
	{
		char log_buf[60];
		ssize_t len;

		len = snprintf(log_buf, sizeof(log_buf),
					   "eDRX parameter update: eDRX: %f, PTW: %f",
					   evt->edrx_cfg.edrx, evt->edrx_cfg.ptw);
		if (len > 0)
		{
			LOG_INF("%s", log_buf);
		}
		break;
	}
	case LTE_LC_EVT_RRC_UPDATE:
		LOG_INF("RRC mode: %s",
			   evt->rrc_mode == LTE_LC_RRC_MODE_CONNECTED ? "Connected" : "Idle");
		break;
	case LTE_LC_EVT_CELL_UPDATE:
		LOG_INF("LTE cell changed: Cell ID: %d, Tracking area: %d",
			   evt->cell.id, evt->cell.tac);
		break;
	default:
		break;
	}
}

static int configure_low_power(void)
{
	int err;
	/** Power Saving Mode */
#ifdef CONFIG_UDP_PSM_ENABLE
	err = lte_lc_psm_req(true);
#else
	err = lte_lc_psm_req(false);
#endif /* CONFIG_UDP_PSM_ENABLE */
	if (err)
		{
			LOG_ERR("lte_lc_psm_req, error: %d", err);
		}
	
	
	/** Release Assistance Indication  */
#ifdef CONFIG_UDP_RAI_ENABLE
	err = lte_lc_rai_req(true);
#else
	err = lte_lc_rai_req(false);
#endif /* CONFIG_UDP_RAI_ENABLE */
	if (err)
	{
		LOG_ERR("lte_lc_rai_req, error: %d", err);
	}

	return err;
}

static void modem_init(void)
{
	int err;

	err = lte_lc_init();
	if (err)
	{
		LOG_ERR("Modem initialization failed, error: %d", err);
		return;
	}
	
}

static void modem_connect(void)
{
	int err;

	err = lte_lc_connect_async(lte_handler);
	if (err)
	{
		LOG_ERR("Connecting to LTE network failed, error: %d",
				err);
		return;
	}

}
#endif

static void server_transmission_work_fn(struct k_work *work)
{
	int err;

	//server_connect();
	if (sock < 0)
	{
		LOG_ERR("Socket not connected");
		return;
	}
	LOG_INF("Transmitting UDP/IP payload of %d bytes to the ",
		   CONFIG_UDP_DATA_UPLOAD_SIZE_BYTES + UDP_IP_HEADER_SIZE);
	LOG_INF("IP address %s, port number %d",
		   CONFIG_UDP_SERVER_ADDRESS_STATIC,
		   CONFIG_UDP_SERVER_PORT);

#ifdef CONFIG_UDP_RAI_ENABLE
	err = setsockopt(sock, SOL_SOCKET, SO_RAI_LAST , NULL, 0);
	if (err < 0) {
		LOG_ERR("Failed to set socket options, %d", errno);
		return;	
	}
#endif

	err =  send(sock, MESSAGE_TO_SEND, SSTRLEN(MESSAGE_TO_SEND), 0);
	if (err < 0)
	{
		LOG_ERR("Failed to transmit UDP packet, %d", err);
		return;
	}
	//server_disconnect();
}

static void work_init(void)
{

	k_work_init_delayable(&server_transmission_work,
						  server_transmission_work_fn);
}

void main(void)
{
	int err;
	int received;
	LOG_INF("UDP sample has started");

	button_init();

	LTE_Connection_Current_State = LTE_STATE_BUSY;

#if defined(CONFIG_NRF_MODEM_LIB)
	/* Initialize the modem before calling configure_low_power(). This is
	 * because the enabling of RAI is dependent on the
	 * configured network mode which is set during modem initialization.
	 */
	modem_init();
	err = configure_low_power();
	if (err)
	{
		LOG_ERR("Unable to set low power configuration, error: %d",
			   err);
	}
	modem_connect();
#endif
	while (LTE_STATE_BUSY == LTE_Connection_Current_State)
	{
		LOG_WRN("lte_set_connection BUSY!");
		k_sleep(K_SECONDS(3));
	}

	if (server_resolve() != 0) {
		LOG_INF("Failed to resolve server name");
		return;
	}
	
	if (server_connect() != 0) {
		LOG_INF("Failed to initialize client");
		return;
	}

	work_init();
	while (1) {
			/* STEP 10 - Call recv() to listen to received messages */
			received = recv(sock, recv_buf, sizeof(recv_buf) - 1, 0);

			if (received < 0) {
				LOG_ERR("Socket error: %d, exit", errno);
				break;
			}

			if (received == 0) {
				LOG_ERR("Empty datagram");
				break;
			}

			recv_buf[received] = 0;
			LOG_INF("Data received from the server: (%s)", recv_buf);
			
		}
		server_disconnect();
}
