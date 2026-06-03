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

int main(void)
{
	int err;

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

	return 0;
}
