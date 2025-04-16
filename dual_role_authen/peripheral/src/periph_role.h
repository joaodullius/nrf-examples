#ifndef PERIPH_ROLE_H
#define PERIPH_ROLE_H

#include <zephyr/bluetooth/bluetooth.h>


int periph_role_init(void);
void periph_bt_ready(int err);


#endif /* PERIPH_ROLE_H */