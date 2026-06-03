#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/poweroff.h>

#include <hal/nrf_reset.h>
#include <hal/nrf_gpio.h>

#include <nfc_t4t_lib.h>
#include <nfc/ndef/msg.h>
#include <nfc/ndef/text_rec.h>
#include <nfc/t4t/ndef_file.h>

#include <dk_buttons_and_leds.h>

#include <zephyr/drivers/flash.h>
#include <zephyr/fs/zms.h>
#include <zephyr/storage/flash_map.h>

#define BLINK_RATE_DEFAULT_MS  500U
#define BLINK_RATE_MIN_MS      50U
#define BLINK_RATE_MAX_MS      10000U
#define SYSTEM_OFF_DELAY_S     3
#define NDEF_FILE_SIZE         256
#define ZMS_BLINK_RATE_ID      1

/* DK_LED1 = LED0 on board, DK_LED2 = LED1, DK_LED3 = LED2 */
#define LED_BLINK          DK_LED1
#define LED_NFC_FIELD      DK_LED2
#define LED_NFC_CONFIRM    DK_LED3

#define NFC_CONFIRM_MS     100
#define NFC_CONFIRM_STEPS  6   /* 3 blinks x on+off */

static uint8_t ndef_msg_buf[NDEF_FILE_SIZE];
static uint32_t blink_rate_ms = BLINK_RATE_DEFAULT_MS;
static bool nfc_mode;

static struct k_work_delayable system_off_work;
static struct k_work_delayable led_blink_work;
static struct k_work_delayable confirm_work;
static struct k_work save_work;
static atomic_t pending_rate = ATOMIC_INIT(0);

/* ZMS */
#define ZMS_PARTITION        storage_partition
#define ZMS_PARTITION_DEVICE FIXED_PARTITION_DEVICE(ZMS_PARTITION)
#define ZMS_PARTITION_OFFSET FIXED_PARTITION_OFFSET(ZMS_PARTITION)
#define ZMS_SECTOR_COUNT     2

static struct zms_fs fs;

static int storage_init(void)
{
	struct flash_pages_info info;
	int rc;

	fs.flash_device = ZMS_PARTITION_DEVICE;
	if (!device_is_ready(fs.flash_device)) {
		printk("Storage device not ready\n");
		return -ENODEV;
	}
	fs.offset = ZMS_PARTITION_OFFSET;
	rc = flash_get_page_info_by_offs(fs.flash_device, fs.offset, &info);
	if (rc) {
		printk("Cannot get flash page info (err %d)\n", rc);
		return rc;
	}
	fs.sector_size  = info.size;
	fs.sector_count = ZMS_SECTOR_COUNT;
	return zms_mount(&fs);
}

static uint32_t load_blink_rate(void)
{
	uint32_t rate;
	ssize_t rc = zms_read(&fs, ZMS_BLINK_RATE_ID, &rate, sizeof(rate));

	if (rc == sizeof(rate) &&
	    rate >= BLINK_RATE_MIN_MS &&
	    rate <= BLINK_RATE_MAX_MS) {
		return rate;
	}
	return BLINK_RATE_DEFAULT_MS;
}

static void do_save_rate(struct k_work *work)
{
	uint32_t rate = (uint32_t)atomic_get(&pending_rate);

	if (rate == 0) {
		return;
	}
	atomic_set(&pending_rate, 0);
	blink_rate_ms = rate;
	zms_write(&fs, ZMS_BLINK_RATE_ID, &rate, sizeof(rate));
	printk("Saved blink rate: %u ms\n", rate);
	k_work_reschedule(&confirm_work, K_NO_WAIT);
}

static int build_ndef_text(uint32_t rate)
{
	static const uint8_t en_code[] = {'e', 'n'};
	static char text[32];
	int text_len = snprintf(text, sizeof(text), "LED %u", rate);

	NFC_NDEF_TEXT_RECORD_DESC_DEF(text_rec, UTF_8,
				      en_code, sizeof(en_code),
				      (const uint8_t *)text, text_len);
	NFC_NDEF_MSG_DEF(nfc_text_msg, 1);

	int err = nfc_ndef_msg_record_add(&NFC_NDEF_MSG(nfc_text_msg),
					  &NFC_NDEF_TEXT_RECORD_DESC(text_rec));
	if (err) {
		return err;
	}

	uint32_t ndef_size = nfc_t4t_ndef_file_msg_size_get(sizeof(ndef_msg_buf));

	err = nfc_ndef_msg_encode(&NFC_NDEF_MSG(nfc_text_msg),
				  nfc_t4t_ndef_file_msg_get(ndef_msg_buf),
				  &ndef_size);
	if (err) {
		return err;
	}

	return nfc_t4t_ndef_file_encode(ndef_msg_buf, &ndef_size);
}

static uint32_t parse_blink_rate(void)
{
	const char prefix[] = "LED ";
	size_t prefix_len = strlen(prefix);

	for (size_t i = 0; i + prefix_len < sizeof(ndef_msg_buf); i++) {
		if (memcmp(&ndef_msg_buf[i], prefix, prefix_len) == 0) {
			long val = strtol((const char *)&ndef_msg_buf[i + prefix_len],
					  NULL, 10);
			if (val >= BLINK_RATE_MIN_MS && val <= BLINK_RATE_MAX_MS) {
				return (uint32_t)val;
			}
		}
	}
	return 0;
}

static void nfc_callback(void *context, nfc_t4t_event_t event,
			 const uint8_t *data, size_t data_length,
			 uint32_t flags)
{
	ARG_UNUSED(context);
	ARG_UNUSED(data);
	ARG_UNUSED(flags);

	switch (event) {
	case NFC_T4T_EVENT_FIELD_ON:
		if (nfc_mode) {
			k_work_cancel_delayable(&system_off_work);
			dk_set_led_on(LED_NFC_FIELD);
			printk("NFC field on\n");
		}
		break;

	case NFC_T4T_EVENT_FIELD_OFF:
		if (nfc_mode) {
			dk_set_led_off(LED_NFC_FIELD);
			k_work_reschedule(&system_off_work,
					  K_SECONDS(SYSTEM_OFF_DELAY_S));
			printk("NFC field off, sleeping in %ds\n",
			       SYSTEM_OFF_DELAY_S);
		}
		break;

	case NFC_T4T_EVENT_NDEF_UPDATED:
		if (nfc_mode && data_length > 0) {
			uint32_t rate = parse_blink_rate();

			if (rate > 0) {
				printk("NFC update: LED %u ms\n", rate);
				atomic_set(&pending_rate, rate);
				k_work_submit(&save_work);
			} else {
				printk("NFC update: invalid format"
				       " (expected 'LED <ms>')\n");
			}
		}
		break;

	default:
		break;
	}
}

static void do_system_off(struct k_work *work) { sys_poweroff(); }
static void led_blink_handler(struct k_work *work) {}
static void confirm_blink_handler(struct k_work *work) {}
static void button_handler(uint32_t button_state, uint32_t has_changed)
{
	ARG_UNUSED(button_state);
	ARG_UNUSED(has_changed);
}

int main(void)
{
	int err;

	k_work_init_delayable(&system_off_work, do_system_off);
	k_work_init_delayable(&led_blink_work, led_blink_handler);
	k_work_init_delayable(&confirm_work, confirm_blink_handler);
	k_work_init(&save_work, do_save_rate);

	err = dk_leds_init();
	if (err) {
		printk("Cannot init LEDs (err %d)\n", err);
		return err;
	}

	err = storage_init();
	if (err) {
		printk("Cannot init storage (err %d)\n", err);
		return err;
	}
	blink_rate_ms = load_blink_rate();
	printk("Blink rate loaded: %u ms\n", blink_rate_ms);

	uint32_t reas = nrf_reset_resetreas_get(NRF_RESET);
	nrf_reset_resetreas_clear(NRF_RESET, reas);
	nfc_mode = (reas & NRF_RESET_RESETREAS_NFC_MASK) != 0;
	printk("Wake reason: 0x%08X -> %s mode\n",
	       reas, nfc_mode ? "NFC" : "Active");

	err = build_ndef_text(blink_rate_ms);
	if (err) {
		printk("Cannot build NDEF (err %d)\n", err);
		return err;
	}

	err = nfc_t4t_setup(nfc_callback, NULL);
	if (err < 0) {
		printk("Cannot setup NFC T4T (err %d)\n", err);
		return err;
	}

	err = nfc_t4t_ndef_rwpayload_set(ndef_msg_buf, sizeof(ndef_msg_buf));
	if (err < 0) {
		printk("Cannot set NFC payload (err %d)\n", err);
		return err;
	}

	err = nfc_t4t_emulation_start();
	if (err < 0) {
		printk("Cannot start NFC emulation (err %d)\n", err);
		return err;
	}

	printk("NFC started. Tag content: LED %u\n", blink_rate_ms);

	return 0;
}
