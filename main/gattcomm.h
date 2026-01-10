#pragma once
#include <stdint.h>

void gattcomm_init(void);
void gattcomm_disconnect(void);
void gattcomm_tx(const uint8_t *data, uint16_t length);
