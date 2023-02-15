/*
 * Copyright (c) 2019 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <zephyr/kernel.h>
#include <zephyr/net/socket.h>
#include <dk_buttons_and_leds.h>
#include <zephyr/logging/log.h>
#include <nrf_modem_at.h>
#include <nrf_modem_gnss.h>
#include <modem/lte_lc.h>
#include <date_time.h>

#define SERVER_HOSTNAME "nordicecho.westeurope.cloudapp.azure.com"
#define SERVER_PORT "2444"

#define MESSAGE_SIZE 242 
#define MESSAGE_TO_SEND "Hello"


static int64_t gnss_start_time;
static bool first_fix = false;

static uint8_t gps_data[MESSAGE_SIZE];

static int sock;
static struct sockaddr_storage server;
static uint8_t recv_buf[MESSAGE_SIZE];

static K_SEM_DEFINE(lte_connected, 0, 1);
LOG_MODULE_REGISTER(gnss_sample, CONFIG_GNSS_SAMPLE_LOG_LEVEL);

#define PI 3.14159265358979323846
#define EARTH_RADIUS_METERS (6371.0 * 1000.0)

static struct k_work_q gnss_work_q;

#define GNSS_WORKQ_THREAD_STACK_SIZE 2304
#define GNSS_WORKQ_THREAD_PRIORITY   5

K_THREAD_STACK_DEFINE(gnss_workq_stack_area, GNSS_WORKQ_THREAD_STACK_SIZE);

#include "assistance.h"

static struct nrf_modem_gnss_agps_data_frame last_agps;
static struct k_work agps_data_get_work;
static volatile bool requesting_assistance;

static struct nrf_modem_gnss_pvt_data_frame last_pvt;
static K_SEM_DEFINE(time_sem, 0, 1);


static int server_resolve(void)
{
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

	struct sockaddr_in *server4 = ((struct sockaddr_in *)&server);

	server4->sin_addr.s_addr =
		((struct sockaddr_in *)result->ai_addr)->sin_addr.s_addr;
	server4->sin_family = AF_INET;
	server4->sin_port = ((struct sockaddr_in *)result->ai_addr)->sin_port;
	
	char ipv4_addr[NET_IPV4_ADDR_LEN];
	inet_ntop(AF_INET, &server4->sin_addr.s_addr, ipv4_addr,
		  sizeof(ipv4_addr));
	LOG_INF("IPv4 Address found %s", ipv4_addr);
	
	freeaddrinfo(result);

	return 0;
}

static int server_connect(void)
{
	int err;
	sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (sock < 0) {
		LOG_ERR("Failed to create socket: %d.", errno);
		return -errno;
	}

	err = connect(sock, (struct sockaddr *)&server,
		      sizeof(struct sockaddr_in));
	if (err < 0) {
		LOG_ERR("Connect failed : %d", errno);
		return -errno;
	}
	LOG_INF("Successfully connected to server");

	return 0;
}

static void print_fix_data(struct nrf_modem_gnss_pvt_data_frame *pvt_data)
{
	printf("Latitude:       %.06f\n", pvt_data->latitude);
	printf("Longitude:      %.06f\n", pvt_data->longitude);
	printf("Altitude:       %.01f m\n", pvt_data->altitude);
	printf("Accuracy:       %.01f m\n", pvt_data->accuracy);
	printf("Speed:          %.01f m/s\n", pvt_data->speed);
	printf("Speed accuracy: %.01f m/s\n", pvt_data->speed_accuracy);
	printf("Heading:        %.01f deg\n", pvt_data->heading);
	printf("Date:           %04u-%02u-%02u\n",
	       pvt_data->datetime.year,
	       pvt_data->datetime.month,
	       pvt_data->datetime.day);
	printf("Time (UTC):     %02u:%02u:%02u.%03u\n",
	       pvt_data->datetime.hour,
	       pvt_data->datetime.minute,
	       pvt_data->datetime.seconds,
	       pvt_data->datetime.ms);
	printf("PDOP:           %.01f\n", pvt_data->pdop);
	printf("HDOP:           %.01f\n", pvt_data->hdop);
	printf("VDOP:           %.01f\n", pvt_data->vdop);
	printf("TDOP:           %.01f\n", pvt_data->tdop);

	int err = snprintf(gps_data, MESSAGE_SIZE, "Latitude: %.06f, Longitude: %.06f", pvt_data->latitude, pvt_data->longitude);	
	if (err < 0) {
		LOG_ERR("Failed to print to buffer: %d", err);
	}   

}

// Workqueue handler to send UDP message

void message_work_handler(struct k_work *work)
{
	// Pad gps_data with 0xFF up to MESSAGE_SIZE
   int err = send(sock, &gps_data, MESSAGE_SIZE, 0);
	LOG_HEXDUMP_INF(gps_data, sizeof(gps_data), "gps_data");
	if (err < 0) {
		LOG_INF("Failed to send message, %d", errno);
		return;	
	}
}
K_WORK_DEFINE(message_work, message_work_handler);


static void gnss_event_handler(int event)
{
	int retval, num_satellites;

	switch (event) {
	case NRF_MODEM_GNSS_EVT_PVT:
		num_satellites = 0;
		for (int i = 0; i < 12 ; i++) {
			if (last_pvt.sv[i].signal != 0) {
				LOG_INF("sv: %d, cn0: %d", last_pvt.sv[i].sv, last_pvt.sv[i].cn0);
				num_satellites++;
			}	
		} 
		LOG_INF("Searching. Current satellites: %d", num_satellites);		
		retval = nrf_modem_gnss_read(&last_pvt, sizeof(last_pvt), NRF_MODEM_GNSS_DATA_PVT);
		if (retval) {
			LOG_ERR("nrf_modem_gnss_read failed, err %d", retval);
			return;
		}
		if (last_pvt.flags & NRF_MODEM_GNSS_PVT_FLAG_FIX_VALID) {
			dk_set_led_on(DK_LED1);
			print_fix_data(&last_pvt);
			if (!first_fix) {
				LOG_INF("Time to first fix: %2.1lld s", (k_uptime_get() - gnss_start_time)/1000);
				first_fix = true;
			}
			k_work_submit(&message_work);
			return;
		} 
		if (last_pvt.flags & NRF_MODEM_GNSS_PVT_FLAG_DEADLINE_MISSED) {
			LOG_INF("GNSS blocked by LTE activity");
		} else if (last_pvt.flags & NRF_MODEM_GNSS_PVT_FLAG_NOT_ENOUGH_WINDOW_TIME) {
			LOG_INF("Insufficient GNSS time windows");
		}
		break;

	case NRF_MODEM_GNSS_EVT_PERIODIC_WAKEUP:
		dk_set_led_off(DK_LED1);
		LOG_INF("GNSS has woken up");
		break;
	case NRF_MODEM_GNSS_EVT_SLEEP_AFTER_FIX:
		LOG_INF("GNSS enter sleep after fix");
		break;		
		
	case NRF_MODEM_GNSS_EVT_AGPS_REQ:
		retval = nrf_modem_gnss_read(&last_agps,
					     sizeof(last_agps),
					     NRF_MODEM_GNSS_DATA_AGPS_REQ);
		if (retval == 0) {
			k_work_submit_to_queue(&gnss_work_q, &agps_data_get_work);
		}
		break;		
	default:
		break;
	}
}

static void lte_handler(const struct lte_lc_evt *const evt)
{
	switch (evt->type) {
	case LTE_LC_EVT_NW_REG_STATUS:
		if ((evt->nw_reg_status != LTE_LC_NW_REG_REGISTERED_HOME) &&
			(evt->nw_reg_status != LTE_LC_NW_REG_REGISTERED_ROAMING)) {
			break;
		}
		LOG_INF("Network registration status: %s",
				evt->nw_reg_status == LTE_LC_NW_REG_REGISTERED_HOME ?
				"Connected - home network" : "Connected - roaming");
		k_sem_give(&lte_connected);
		break;	
	case LTE_LC_EVT_RRC_UPDATE:
		LOG_INF("RRC mode: %s", 
				evt->rrc_mode == LTE_LC_RRC_MODE_CONNECTED ? 
				"Connected" : "Idle");
		break;
	/* STEP 9.1 - On event PSM update, print PSM paramters and check if was enabled */
	case LTE_LC_EVT_PSM_UPDATE:
		LOG_INF("PSM parameter update: Periodic TAU: %d s, Active time: %d s",
			evt->psm_cfg.tau, evt->psm_cfg.active_time);
		if (evt->psm_cfg.active_time == -1){
			LOG_ERR("Network rejected PSM parameters. Failed to setup network");
		}
		break;
	/* STEP 9.2 - On event eDRX update, print eDRX paramters */
	case LTE_LC_EVT_EDRX_UPDATE:
		LOG_INF("eDRX parameter update: eDRX: %f, PTW: %f",
			evt->edrx_cfg.edrx, evt->edrx_cfg.ptw);
		break;
	case LTE_LC_EVT_MODEM_SLEEP_ENTER:
		LOG_INF("Modem entering PSM");
		break;
	case LTE_LC_EVT_MODEM_SLEEP_EXIT:
		LOG_INF("Modem exiting PSM");
		break;
	default:
		break;
	}
}
static void agps_data_get_work_fn(struct k_work *item)
{
	ARG_UNUSED(item);

	int err;

	requesting_assistance = true;

	LOG_INF("Assistance data needed, ephe 0x%08x, alm 0x%08x, flags 0x%02x",
		last_agps.sv_mask_ephe,
		last_agps.sv_mask_alm,
		last_agps.data_flags);

	err = assistance_request(&last_agps);
	if (err) {
		LOG_ERR("Failed to request assistance data");
	}
	requesting_assistance = false;
}

static void date_time_evt_handler(const struct date_time_evt *evt)
{
	k_sem_give(&time_sem);
}

static int modem_init(void)
{
	int err;

	date_time_register_handler(date_time_evt_handler);
	
	err = lte_lc_psm_req(true);
	if (err) {
		LOG_ERR("lte_lc_psm_req, error: %d", err);
	} 
	err = lte_lc_edrx_req(true);
	if (err) {
		LOG_ERR("lte_lc_edrx_req, error: %d", err);
	}
	/** Release Assistance Indication  */
	err = lte_lc_rai_req(true);
	if (err) {
		printk("lte_lc_rai_req, error: %d\n", err);
	}

	LOG_INF("Connecting to LTE network");

	err = lte_lc_init_and_connect_async(lte_handler);
	if (err) {
		LOG_ERR("Modem could not be configured, error: %d", err);
		return -1;
	}
	k_sem_take(&lte_connected, K_FOREVER);
	LOG_INF("Connected to LTE network");


	LOG_INF("Waiting for current time");

	/* Wait for an event from the Date Time library. */
	k_sem_take(&time_sem, K_MINUTES(10));

	if (!date_time_is_valid()) {
		LOG_WRN("Failed to get current time, continuing anyway");
	} else {
		LOG_INF("Got current time");
	}

	dk_set_led_on(DK_LED2);
	return 0;
}

static int sample_init(void)
{
	int err = 0;


	struct k_work_queue_config cfg = {
		.name = "gnss_work_q",
		.no_yield = false
	};

	k_work_queue_start(
		&gnss_work_q,
		gnss_workq_stack_area,
		K_THREAD_STACK_SIZEOF(gnss_workq_stack_area),
		GNSS_WORKQ_THREAD_PRIORITY,
		&cfg);

	k_work_init(&agps_data_get_work, agps_data_get_work_fn);

	err = assistance_init(&gnss_work_q);

	return err;
}

static int gnss_init_and_start(void)
{
	
	/* STEP 4 - Set the modem mode to normal */
	if (lte_lc_func_mode_set(LTE_LC_FUNC_MODE_NORMAL) != 0) {
		LOG_ERR("Failed to activate GNSS functional mode");
		return -1;
	}

	if (nrf_modem_gnss_event_handler_set(gnss_event_handler) != 0) {
		LOG_ERR("Failed to set GNSS event handler");
		return -1;
	}
	/* This use case flag should always be set. */
	uint8_t use_case = NRF_MODEM_GNSS_USE_CASE_MULTIPLE_HOT_START;

	/* Disable GNSS scheduled downloads when assistance is used. */
	use_case |= NRF_MODEM_GNSS_USE_CASE_SCHED_DOWNLOAD_DISABLE;

	if (nrf_modem_gnss_use_case_set(use_case) != 0) {
		LOG_WRN("Failed to set GNSS use case");
	}

	/* Default to continuous tracking. */
	uint16_t fix_retry = 0;
	uint16_t fix_interval = 1;

	fix_retry = CONFIG_GNSS_SAMPLE_PERIODIC_TIMEOUT;
	fix_interval = CONFIG_GNSS_SAMPLE_PERIODIC_INTERVAL;

	if (nrf_modem_gnss_fix_retry_set(fix_retry) != 0) {
		LOG_ERR("Failed to set GNSS fix retry");
		return -1;
	}

	if (nrf_modem_gnss_fix_interval_set(fix_interval) != 0) {
		LOG_ERR("Failed to set GNSS fix interval");
		return -1;
	}

	LOG_INF("Starting GNSS");
	if (nrf_modem_gnss_start() != 0) {
		LOG_ERR("Failed to start GNSS");
		return -1;
	}

	gnss_start_time = k_uptime_get();

	return 0;
}

static void button_handler(uint32_t button_state, uint32_t has_changed) 
{
	/* STEP 3.3 - Upon button 1 push, send gps_data */
	switch (has_changed) {
	case DK_BTN1_MSK:
		if (button_state & DK_BTN1_MSK){	
			k_work_submit(&message_work);
		}
		break;
	}
}

void main(void)
{

	LOG_INF("Starting GNSS sample");
	int received;
	
	if (dk_leds_init() != 0) {
		LOG_ERR("Failed to initialize the LED library");
	}

	if (modem_init() != 0) {
		LOG_ERR("Failed to initialize modem");
		return -1;
	}

	if (dk_buttons_init(button_handler) != 0) {
		LOG_ERR("Failed to initialize the buttons library");
	}

	if (sample_init() != 0) {
		LOG_ERR("Failed to initialize sample");
		return -1;
	}
	
	
	
	if (server_resolve() != 0) {
		LOG_INF("Failed to resolve server name");
		return -1;
	}
	
	if (server_connect() != 0) {
		LOG_INF("Failed to initialize client");
		return -1;
	}
	

	if (gnss_init_and_start() != 0) {
		LOG_ERR("Failed to initialize and start GNSS");
		return -1;
	}

	while (1) {
		
		received = recv(sock, recv_buf, sizeof(recv_buf) - 1, 0);
		LOG_HEXDUMP_INF(recv_buf, sizeof(recv_buf), "recv_buf");

		if (received < 0) {
			LOG_ERR("Socket error: %d, exit", errno);
			break;
		}

		if (received == 0) {
			break;
		}

		recv_buf[received] = 0;
		LOG_INF("Data received from the server: (%s)", recv_buf);
		

	}
	(void)close(sock);
}
