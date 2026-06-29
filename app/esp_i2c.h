#pragma once

#include <stdint.h>
#include "keyboard.h"

#define REG_ID_ESP_KEY_EVENT 0x70

void esp_i2c_push_key(char key, enum key_state state);
void esp_i2c_pop_key(uint8_t *buffer, uint8_t *len);
