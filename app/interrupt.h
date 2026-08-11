#pragma once

#include <stdbool.h>

void interrupt_set_esp_pending(bool pending);
void interrupt_init(void);
