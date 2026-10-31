#include <pico/stdlib.h>
#include <stdio.h>
#include <tusb.h>

#include "backlight.h"
#include "debug.h"
#include "gpioexp.h"
#include "interrupt.h"
#include "keyboard.h"
#include "puppet_i2c.h"
#include "reg.h"
#include "touchpad.h"
#include "usb.h"

// since the SDK doesn't support per-GPIO irq, we use this global irq and forward it
static void gpio_irq(uint gpio, uint32_t events)
{
//	printf("%s: gpio %d, events 0x%02X\r\n", __func__, gpio, events);
	touchpad_gpio_irq(gpio, events);
	gpioexp_gpio_irq(gpio, events);
}

// TODO: Microphone
int main(void)
{
	// The here order is important because it determines callback call order
	usb_init();

#ifndef NDEBUG
	debug_init();
#endif

	reg_init();

	// Install global BANK0 GPIO IRQ callback before individual GPIO IRQs.
	// The pinned Pico SDK indexes GPIO registers even with events=0;
	// GPIO 0xFF used previously is not a valid RP2040 pin.
	gpio_set_irq_enabled_with_callback(PIN_INT, 0, true, &gpio_irq);

	backlight_init();

	gpioexp_init();

	keyboard_init();

	touchpad_init();

	interrupt_init();

	puppet_i2c_init();

#ifndef NDEBUG
	printf("Starting main loop\r\n");
#endif

	while (true) {
		__wfe();
	}

	return 0;
}
