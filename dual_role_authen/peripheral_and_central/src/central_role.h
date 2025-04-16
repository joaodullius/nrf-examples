#ifndef CENTRAL_ROLE_H
#define CENTRAL_ROLE_H

#include <zephyr/bluetooth/bluetooth.h>

int central_role_init(void);
uint16_t central_role_get_remote_led_handle(void);
void central_role_write_led(uint8_t value);



#endif /* CENTRAL_ROLE_H */