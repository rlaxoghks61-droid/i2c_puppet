#include "interrupt.h"

#include "app_config.h"
#include "gpioexp.h"
#include "keyboard.h"
#include "reg.h"
#include "touchpad.h"

#include <hardware/sync.h>
#include <pico/stdlib.h>

#define INT_SOURCE_PULSE (1u << 0)
#define INT_SOURCE_ESP   (1u << 1)

static volatile uint32_t int_sources = 0;
static bool int_line_initialized = false;

static void set_int_source(uint32_t source, bool active)
{
	const uint32_t irq_state = save_and_disable_interrupts();

	if (active)
		int_sources |= source;
	else
		int_sources &= ~source;

	if (int_line_initialized)
		gpio_put(PIN_INT, int_sources ? 0 : 1);

	restore_interrupts(irq_state);
}

void interrupt_set_esp_pending(bool pending)
{
	set_int_source(INT_SOURCE_ESP, pending);
}

static void pulse_int_line(void)
{
	set_int_source(INT_SOURCE_PULSE, true);
	busy_wait_ms(reg_get_value(REG_ID_IND));
	set_int_source(INT_SOURCE_PULSE, false);
}

static void key_cb(char key, enum key_state state)
{
	(void)key;
	(void)state;

	if (!reg_is_bit_set(REG_ID_CFG, CFG_KEY_INT))
		return;

	reg_set_bit(REG_ID_INT, INT_KEY);
	pulse_int_line();
}
static struct key_callback key_callback = { .func = key_cb };

static void key_lock_cb(bool caps_changed, bool num_changed)
{
	bool do_int = false;

	if (caps_changed && reg_is_bit_set(REG_ID_CFG, CFG_CAPSLOCK_INT)) {
		reg_set_bit(REG_ID_INT, INT_CAPSLOCK);
		do_int = true;
	}

	if (num_changed && reg_is_bit_set(REG_ID_CFG, CFG_NUMLOCK_INT)) {
		reg_set_bit(REG_ID_INT, INT_NUMLOCK);
		do_int = true;
	}

	if (do_int)
		pulse_int_line();
}
static struct key_lock_callback key_lock_callback = { .func = key_lock_cb };

static void touch_cb(int8_t x, int8_t y)
{
	(void)x;
	(void)y;

	if (!reg_is_bit_set(REG_ID_CF2, CF2_TOUCH_INT))
		return;

	reg_set_bit(REG_ID_INT, INT_TOUCH);
	pulse_int_line();
}
static struct touch_callback touch_callback = { .func = touch_cb };

static void gpioexp_cb(uint8_t gpio, uint8_t gpio_idx)
{
	(void)gpio;

	if (!reg_is_bit_set(REG_ID_GIC, (1 << gpio_idx)))
		return;

	reg_set_bit(REG_ID_INT, INT_GPIO);
	reg_set_bit(REG_ID_GIN, (1 << gpio_idx));
	pulse_int_line();
}
static struct gpioexp_callback gpioexp_callback = { .func = gpioexp_cb };

void interrupt_init(void)
{
	const uint32_t irq_state = save_and_disable_interrupts();

	gpio_init(PIN_INT);
	gpio_set_dir(PIN_INT, GPIO_OUT);
	gpio_pull_up(PIN_INT);

	int_line_initialized = true;
	gpio_put(PIN_INT, int_sources ? 0 : 1);

	restore_interrupts(irq_state);

	keyboard_add_key_callback(&key_callback);
	keyboard_add_lock_callback(&key_lock_callback);

	touchpad_add_touch_callback(&touch_callback);

	gpioexp_add_int_callback(&gpioexp_callback);
}
