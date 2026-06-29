#pragma once

#include <stdint.h>

#define REG_ID_ESP_KEY_EVENT 0x70

#define ESP_EVT_KEY      1
#define ESP_EVT_MOUSE    2
#define ESP_EVT_CONSUMER 3

void esp_i2c_push_hid(uint8_t modifier, uint8_t keycode, uint8_t state);
void esp_i2c_push_mouse(int8_t x, int8_t y, uint8_t buttons);
void esp_i2c_push_consumer(uint16_t usage, uint8_t state);

void esp_i2c_pop_key(uint8_t *buffer, uint8_t *len);
