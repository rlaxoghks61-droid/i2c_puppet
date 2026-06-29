#pragma once

#include <stdint.h>

#define REG_ID_ESP_KEY_EVENT 0x70

void esp_i2c_push_hid(uint8_t modifier, uint8_t keycode, uint8_t state);
void esp_i2c_pop_key(uint8_t *buffer, uint8_t *len);
