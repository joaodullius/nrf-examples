#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/drivers/sensor.h>
#include <stdio.h>

static void imu_trigger_handler(const struct device *dev,
	const struct sensor_trigger *trig)
{
	struct sensor_value accel[3];
	sensor_sample_fetch(dev);
	sensor_channel_get(dev, SENSOR_CHAN_ACCEL_XYZ, accel);
	printf("%.3f\t%.3f\t%.3f\r\n",
		sensor_value_to_double(&accel[0]),
		sensor_value_to_double(&accel[1]),
		sensor_value_to_double(&accel[2]));
}


int main() {
    setvbuf(stdout, NULL, _IONBF, 0);

    const struct device *imu = DEVICE_DT_GET_ONE(adi_adxl362);
    if (!device_is_ready(imu)) {
        printf("imu device not ready\n");
        return 1;
    }

	// Set scale to ±2g
	struct sensor_value scale_val = {
		.val1 = 2,
		.val2 = 0,
	};
	sensor_attr_set(imu, SENSOR_CHAN_ACCEL_XYZ, SENSOR_ATTR_FULL_SCALE, &scale_val);

	// Set ODR to 400 Hz
	struct sensor_value odr_val = {
		.val1 = 400,
		.val2 = 0,
	};
	sensor_attr_set(imu, SENSOR_CHAN_ACCEL_XYZ, SENSOR_ATTR_SAMPLING_FREQUENCY, &odr_val);

	struct sensor_trigger trig = {
        .type = SENSOR_TRIG_DATA_READY,
        .chan = SENSOR_CHAN_ACCEL_XYZ,
    };

    sensor_trigger_set(imu, &trig, imu_trigger_handler);
}
