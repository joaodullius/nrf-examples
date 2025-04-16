#ifndef LED_SERVICE_H
#define LED_SERVICE_H

#include <zephyr/bluetooth/bluetooth.h>

// Service UUID
#define LED_SERVICE_UUID BT_UUID_128_ENCODE(0x227a5db3, 0xcae4, 0x435f, 0x8ed1, 0x2db3fbe438c9)

// Led State characteristic
#define LED_STATE_CHAR_UUID BT_UUID_128_ENCODE(0x22f64492, 0xbb02, 0x4ccf, 0x9612, 0xc749be0c897d)

int led_service_init(void);

#endif /* LED_SERVICE_H */