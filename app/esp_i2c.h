#pragma once

#include <stdint.h>
#include "keyboard.h"

void esp_i2c_init(void);
void esp_i2c_push_key(char key, enum key_state state);
