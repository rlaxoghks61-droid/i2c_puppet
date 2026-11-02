// Isolated RP2040 I2C slave probe (not full keyboard firmware).
// No USB, touchpad, keyboard, timers, or GPIO-expander initialization.
// GPIO28 SDA, GPIO29 SCL; 7-bit address 0x1f; register 0x01.
#include <pico/stdlib.h>
#include "reg.h"
#include "puppet_i2c.h"
#include "esp_i2c.h"
int main(void) {
    reg_init();
    esp_i2c_init();
    puppet_i2c_init();
    while (true) {
        tight_loop_contents();
    }
}
