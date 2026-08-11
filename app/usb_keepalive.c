#include "usb_keepalive.h"

#include "keyboard.h"
#include "reg.h"
#include "touchpad.h"
#include "usb.h"

#include <hardware/irq.h>
#include <pico/mutex.h>
#include <pico/stdlib.h>
#include <tusb.h>

#define USB_KEEPALIVE_IRQ 30
#define USB_KEEPALIVE_INTERVAL_US 10000000
#define USB_KEEPALIVE_ACTIVITY_GUARD_MS 100

static volatile uint8_t keys_down = 0;
static volatile uint32_t last_activity_ms = 0;

static void note_activity(void)
{
	last_activity_ms = to_ms_since_boot(get_absolute_time());
}

static void keepalive_key_cb(char key, enum key_state state)
{
	(void)key;
	note_activity();

	if (state == KEY_STATE_PRESSED)
	{
		if (keys_down < UINT8_MAX)
			keys_down++;
	}
	else if (state == KEY_STATE_RELEASED)
	{
		if (keys_down > 0)
			keys_down--;
	}
}

static struct key_callback keepalive_key_callback = { .func = keepalive_key_cb };

static void keepalive_touch_cb(int8_t x, int8_t y)
{
	(void)x;
	(void)y;
	note_activity();
}

static struct touch_callback keepalive_touch_callback = { .func = keepalive_touch_cb };

static void keepalive_irq(void)
{
	uint32_t now_ms = to_ms_since_boot(get_absolute_time());

	if (keys_down != 0 ||
		(now_ms - last_activity_ms) < USB_KEEPALIVE_ACTIVITY_GUARD_MS)
	{
		return;
	}

	mutex_t *mutex = usb_get_mutex();
	if (!mutex_try_enter(mutex, NULL))
		return;

	if (tud_mounted() &&
		!tud_suspended() &&
		reg_is_bit_set(REG_ID_CF2, CF2_USB_KEYB_ON) &&
		tud_hid_n_ready(USB_ITF_KEYBOARD))
	{
		uint8_t empty[6] = {0};
		tud_hid_n_keyboard_report(USB_ITF_KEYBOARD, 0, 0, empty);
	}

	mutex_exit(mutex);
}

static int64_t keepalive_timer(alarm_id_t id, void *user_data)
{
	(void)id;
	(void)user_data;

	irq_set_pending(USB_KEEPALIVE_IRQ);
	return USB_KEEPALIVE_INTERVAL_US;
}

void usb_keepalive_init(void)
{
	last_activity_ms = to_ms_since_boot(get_absolute_time());

	keyboard_add_key_callback(&keepalive_key_callback);
	touchpad_add_touch_callback(&keepalive_touch_callback);

	irq_set_exclusive_handler(USB_KEEPALIVE_IRQ, keepalive_irq);
	irq_set_enabled(USB_KEEPALIVE_IRQ, true);

	add_alarm_in_us(USB_KEEPALIVE_INTERVAL_US, keepalive_timer, NULL, true);
}
