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
#include <zephyr/net/coap.h>
#include <zephyr/random/rand32.h>

#define UDP_IP_HEADER_SIZE 28

LOG_MODULE_REGISTER(gnss_udp, LOG_LEVEL_INF);


#define BT1_NODE DT_ALIAS(sw0)

static const struct gpio_dt_spec buttons[] = {
	GPIO_DT_SPEC_GET_OR(BT1_NODE, gpios,
						{0})};
static struct gpio_callback button_cb_data[4];

#define MESSAGE_SIZE 256 
#define MESSAGE_TO_SEND "Hello from GNSS UDP"
#define SSTRLEN(s) (sizeof(s) - 1)

static int sock;
static struct sockaddr_storage server;

static volatile enum state_type { LTE_STATE_ON,
								  LTE_STATE_OFFLINE,
								  LTE_STATE_BUSY } LTE_Connection_Current_State;
static volatile enum state_type LTE_Connection_Target_State;

//CoAP Definitions
static uint16_t next_token;
#define APP_COAP_VERSION 1
#define APP_COAP_MAX_MSG_LEN 1280
static uint8_t coap_buf[APP_COAP_MAX_MSG_LEN];
#define CONFIG_COAP_SERVER_HOSTNAME "californium.eclipseprojects.io"
#define CONFIG_COAP_SERVER_PORT 5683
#define CONFIG_COAP_TX_RESOURCE "large-update"





static int server_resolve(void)
{
	/* STEP 6.1 - Call getaddrinfo() to get the IP address of the echo server */
	int err;
	struct addrinfo *result;
	struct addrinfo hints = {
		.ai_family = AF_INET,
		.ai_socktype = SOCK_DGRAM
	};
	char ipv4_addr[NET_IPV4_ADDR_LEN];

	err = getaddrinfo(CONFIG_COAP_SERVER_HOSTNAME, NULL, &hints, &result);
	if (err != 0) {
		LOG_ERR("ERROR: getaddrinfo failed %d", err);
		return -EIO;
	}

	if (result == NULL) {
		LOG_ERR("ERROR: Address not found");
		return -ENOENT;
	}

	/* IPv4 Address. */
	struct sockaddr_in *server4 = ((struct sockaddr_in *)&server);

	server4->sin_addr.s_addr =
		((struct sockaddr_in *)result->ai_addr)->sin_addr.s_addr;
	server4->sin_family = AF_INET;
	server4->sin_port = htons(CONFIG_COAP_SERVER_PORT);

	inet_ntop(AF_INET, &server4->sin_addr.s_addr, ipv4_addr,
		  sizeof(ipv4_addr));
	LOG_INF("IPv4 Address found %s", ipv4_addr);

	/* Free the address. */
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
	LOG_INF("Connected to %s", CONFIG_COAP_SERVER_HOSTNAME);

	next_token = sys_rand32_get();

	return 0;

error:
	server_disconnect();

	return err;
}

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

static void coap_put_work_fn(struct k_work *work)
{
	int err;
	struct coap_packet request;
	if (sock < 0)
	{
		LOG_ERR("Socket not connected");
		return;
	}
	
	next_token++;

	/* STEP 8.1 - Initialize the CoAP packet and append the resource path */ 
	err = coap_packet_init(&request, coap_buf, sizeof(coap_buf),
			       APP_COAP_VERSION, COAP_TYPE_NON_CON,
			       sizeof(next_token), (uint8_t *)&next_token,
			       COAP_METHOD_PUT, coap_next_id());
	if (err < 0) {
		LOG_ERR("Failed to create CoAP request, %d", err);
		return;
	}

	err = coap_packet_append_option(&request, COAP_OPTION_URI_PATH,
					(uint8_t *)CONFIG_COAP_TX_RESOURCE,
					strlen(CONFIG_COAP_TX_RESOURCE));
	if (err < 0) {
		LOG_ERR("Failed to encode CoAP option, %d", err);
		return;
	}

	/* STEP 8.2 - Append the content format as plain text */
	const uint8_t text_plain = COAP_CONTENT_FORMAT_TEXT_PLAIN;
	err = coap_packet_append_option(&request, COAP_OPTION_CONTENT_FORMAT,
					&text_plain,
					sizeof(text_plain));
	if (err < 0) {
		LOG_ERR("Failed to encode CoAP option, %d", err);
		return;
	}

	/* STEP 8.3 - Add the payload to the message */
	err = coap_packet_append_payload_marker(&request);
	if (err < 0) {
		LOG_ERR("Failed to append payload marker, %d", err);
		return;
	}

	char buffer[sizeof(MESSAGE_TO_SEND) + 8];
    sprintf(buffer, "%s - %04X%c", MESSAGE_TO_SEND, next_token, '\0');
	LOG_HEXDUMP_INF(buffer, sizeof(buffer), "Payload");
	err = coap_packet_append_payload(&request, (uint8_t *)buffer, sizeof(buffer));
	if (err < 0) {
		LOG_ERR("Failed to append payload, %d", err);
		return;
	}


#ifdef CONFIG_UDP_RAI_ENABLE
	err = setsockopt(sock, SOL_SOCKET, SO_RAI_LAST , NULL, 0);
	if (err < 0) {
		LOG_ERR("Failed to set socket options, %d", errno);
		return;	
	}
#endif

	err =  send(sock, request.data, request.offset, 0);
	if (err < 0)
	{
		LOG_ERR("Failed to transmit UDP packet, %d", err);
		return;
	}
	//server_disconnect();
}
K_WORK_DEFINE(coap_put_work, coap_put_work_fn);

void button_pressed(const struct device *dev, struct gpio_callback *cb,
					uint32_t pins)
{
	int val;

	LOG_INF("Button pressed at %" PRIu32, k_cycle_get_32());

	val = gpio_pin_get_dt(&buttons[0]);
	if (val == 1 && LTE_Connection_Current_State == LTE_STATE_ON) // button1 pressed
	{
		LOG_INF("Send UDP package!");
		k_work_submit(&coap_put_work);
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



static int client_handle_response(uint8_t *buf, int received)
{
	struct coap_packet reply;
	uint8_t token[8];
	uint16_t token_len;
	const uint8_t *payload;
	uint16_t payload_len;
	uint8_t temp_buf[128];
	/* STEP 9.1 - Parse the received CoAP packet */
	int err = coap_packet_parse(&reply, buf, received, NULL, 0);
	if (err < 0) {
		LOG_ERR("Malformed response received: %d", err);
		return err;
	}

	/* STEP 9.2 - Confirm the token in the response matches the token sent */
	token_len = coap_header_get_token(&reply, token);
	if ((token_len != sizeof(next_token)) ||
	    (memcmp(&next_token, token, sizeof(next_token)) != 0)) {
		LOG_ERR("Invalid token received: 0x%02x%02x",
		       token[1], token[0]);
		return 0;
	}

	/* STEP 9.3 - Retrieve the payload and confirm it's nonzero */
	payload = coap_packet_get_payload(&reply, &payload_len);

	if (payload_len > 0) {
		snprintf(temp_buf, MIN(payload_len + 1, sizeof(temp_buf)), "%s", payload);
	} else {
		strcpy(temp_buf, "EMPTY");
	}

	/* STEP 9.4 - Log the header code, token and payload of the response */
	LOG_INF("CoAP response: Code 0x%x, Token 0x%02x%02x, Payload: %s",
	       coap_header_get_code(&reply), token[1], token[0], (char *)temp_buf);

	return 0;
}

void main(void)
{
	int err;
	int received;
	LOG_INF("UDP sample has started");

	button_init();

	LTE_Connection_Current_State = LTE_STATE_BUSY;

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


	while (1) {
		received = recv(sock, coap_buf, sizeof(coap_buf), 0);

		if (received < 0) {
			LOG_ERR("Socket error: %d, exit", errno);
			break;
		}
		
		if (received == 0) {
			LOG_INF("Empty datagram");
			continue;
		}

		/* STEP 12 - Parse the received CoAP packet */
		err = client_handle_response(coap_buf, received);
		if (err < 0) {
			LOG_ERR("Invalid response, exit");
			break;
		
		}
		
		}
	server_disconnect();
}
