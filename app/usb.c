#include "esp_i2c.h"
#include "usb.h"

#include "backlight.h"
#include "keyboard.h"
#include "touchpad.h"
#include "reg.h"

#include <string.h>
#include <hardware/irq.h>
#include <pico/mutex.h>
#include <pico/stdlib.h>
#include <tusb.h>

#define USB_LOW_PRIORITY_IRQ	31
#define USB_TASK_INTERVAL_US	1000

static struct
{
	mutex_t mutex;
	bool mouse_moved;
	uint8_t mouse_btn;
	uint8_t write_buffer[2];
	uint8_t write_len;
} self;

static int16_t nav_acc_x = 0;
static int16_t nav_acc_y = 0;
static uint32_t nav_block_until_ms = 0;
static bool nav_release_pending = false;
static uint32_t nav_release_time_ms = 0;
static bool alt_pressed = false;
static bool sym_pressed = false;
static bool sym_used = false;
static uint8_t bkl_step = 3;
static uint32_t last_key_time_ms = 0;
static bool bkl_auto_off = false;

static void low_priority_worker_irq(void)
{
	if (mutex_try_enter(&self.mutex, NULL)) {
		tud_task();

		uint32_t now_ms = to_ms_since_boot(get_absolute_time());

		if (!bkl_auto_off &&
			bkl_step != 0 &&
			(now_ms - last_key_time_ms) > 30000)
		{
			reg_set_value(REG_ID_BKL, 0);
			backlight_sync();
			bkl_auto_off = true;
		}

		if (nav_release_pending) {
			if (now_ms >= nav_release_time_ms) {
				uint8_t empty[6] = {0};
				tud_hid_n_keyboard_report(USB_ITF_KEYBOARD, 0, 0, empty);
				nav_release_pending = false;
			}
		}

		mutex_exit(&self.mutex);
	}
}

static int64_t timer_task(alarm_id_t id, void *user_data)
{
	(void)id;
	(void)user_data;

	irq_set_pending(USB_LOW_PRIORITY_IRQ);

	return USB_TASK_INTERVAL_US;
}

static void key_cb(char key, enum key_state state)
{
	if (state == KEY_STATE_PRESSED)
	{
		last_key_time_ms = to_ms_since_boot(get_absolute_time());

		if (bkl_auto_off)
		{
			switch (bkl_step)
			{
				case 0: reg_set_value(REG_ID_BKL, 0); break;
				case 1: reg_set_value(REG_ID_BKL, 85); break;
				case 2: reg_set_value(REG_ID_BKL, 170); break;
				case 3: reg_set_value(REG_ID_BKL, 255); break;
			}

			backlight_sync();
			bkl_auto_off = false;
		}
	}

	if (key == KEY_MOD_ALT)
	{
		alt_pressed = (state != KEY_STATE_RELEASED);

		if (state != KEY_STATE_HOLD)
			esp_i2c_push_hid(KEYBOARD_MODIFIER_LEFTALT, 0, (uint8_t)state);

		return;
	}

	if (key == KEY_MOD_SHL)
	{
		return;
	}

	if (key == KEY_MOD_SHR)
	{
		return;
	}

	if (key == KEY_MOD_SYM)
	{
		if (state == KEY_STATE_PRESSED)
		{
			sym_pressed = true;
			sym_used = false;
		}
		else if (state == KEY_STATE_RELEASED)
		{
			if (!sym_used)
			{
				esp_i2c_push_hid(KEYBOARD_MODIFIER_LEFTSHIFT, HID_KEY_SPACE, KEY_STATE_PRESSED);
				esp_i2c_push_hid(KEYBOARD_MODIFIER_LEFTSHIFT, HID_KEY_SPACE, KEY_STATE_RELEASED);
			}

			sym_pressed = false;
			sym_used = false;
		}

		return;
	}

	if (sym_pressed && state == KEY_STATE_PRESSED)
	{
		sym_used = true;
	}

	if (alt_pressed && key == KEY_BTN_RIGHT2)
	{
		if (state == KEY_STATE_PRESSED)
		{
			bkl_step++;

			if (bkl_step > 3)
				bkl_step = 0;

			switch (bkl_step)
			{
				case 0: reg_set_value(REG_ID_BKL, 0); break;
				case 1: reg_set_value(REG_ID_BKL, 85); break;
				case 2: reg_set_value(REG_ID_BKL, 170); break;
				case 3: reg_set_value(REG_ID_BKL, 255); break;
			}

			backlight_sync();
		}

		return;
	}

	uint16_t consumer_key = 0;

	if (key == KEY_BTN_LEFT1)
		consumer_key = 0x00CD;
	else if (key == KEY_BTN_LEFT2)
		consumer_key = HID_USAGE_CONSUMER_AC_HOME;
	else if (key == KEY_BTN_RIGHT1)
		consumer_key = 0x0224;
	else if (key == KEY_BTN_RIGHT2)
		consumer_key = 0x0030;

	if (consumer_key != 0)
	{
		if (state != KEY_STATE_HOLD)
		{
			esp_i2c_push_consumer(consumer_key, (uint8_t)state);

			if (tud_hid_n_ready(USB_ITF_CONSUMER))
			{
				uint16_t report_key = 0;

				if (state == KEY_STATE_PRESSED)
					report_key = consumer_key;

				tud_hid_n_report(
					USB_ITF_CONSUMER,
					0,
					&report_key,
					sizeof(report_key)
				);
			}
		}

		return;
	}

	if (key == KEY_JOY_CENTER)
	{
		if (keyboard_get_capslock())
		{
			if (state == KEY_STATE_PRESSED)
			{
				uint8_t keycode[6] = {0};
				keycode[0] = HID_KEY_ENTER;

				esp_i2c_push_hid(0, HID_KEY_ENTER, KEY_STATE_PRESSED);
				esp_i2c_push_hid(0, HID_KEY_ENTER, KEY_STATE_RELEASED);

				if (tud_hid_n_ready(USB_ITF_KEYBOARD) &&
					reg_is_bit_set(REG_ID_CF2, CF2_USB_KEYB_ON))
				{
					tud_hid_n_keyboard_report(USB_ITF_KEYBOARD, 0, 0, keycode);

					nav_release_pending = true;
					nav_release_time_ms = to_ms_since_boot(get_absolute_time()) + 30;
				}
			}

			return;
		}

		if (state == KEY_STATE_PRESSED)
		{
			self.mouse_btn = MOUSE_BUTTON_LEFT;
			self.mouse_moved = false;

			esp_i2c_push_mouse(0, 0, MOUSE_BUTTON_LEFT);

			if (tud_hid_n_ready(USB_ITF_MOUSE) &&
				reg_is_bit_set(REG_ID_CF2, CF2_USB_MOUSE_ON))
			{
				tud_hid_n_mouse_report(USB_ITF_MOUSE, 0, MOUSE_BUTTON_LEFT, 0, 0, 0, 0);
			}
		}
		else if ((state == KEY_STATE_HOLD) && !self.mouse_moved)
		{
			self.mouse_btn = MOUSE_BUTTON_RIGHT;

			esp_i2c_push_mouse(0, 0, MOUSE_BUTTON_RIGHT);

			if (tud_hid_n_ready(USB_ITF_MOUSE) &&
				reg_is_bit_set(REG_ID_CF2, CF2_USB_MOUSE_ON))
			{
				tud_hid_n_mouse_report(USB_ITF_MOUSE, 0, MOUSE_BUTTON_RIGHT, 0, 0, 0, 0);
			}
		}
		else if (state == KEY_STATE_RELEASED)
		{
			self.mouse_btn = 0x00;

			esp_i2c_push_mouse(0, 0, 0x00);

			if (tud_hid_n_ready(USB_ITF_MOUSE) &&
				reg_is_bit_set(REG_ID_CF2, CF2_USB_MOUSE_ON))
			{
				tud_hid_n_mouse_report(USB_ITF_MOUSE, 0, 0x00, 0, 0, 0, 0);
			}
		}

		return;
	}

	uint8_t conv_table[128][2] = { HID_ASCII_TO_KEYCODE };
	conv_table['\n'][1] = HID_KEY_ENTER;
	conv_table['\b'][1] = HID_KEY_BACKSPACE;
	conv_table[KEY_JOY_UP][1] = HID_KEY_ARROW_UP;
	conv_table[KEY_JOY_DOWN][1] = HID_KEY_ARROW_DOWN;
	conv_table[KEY_JOY_LEFT][1] = HID_KEY_ARROW_LEFT;
	conv_table[KEY_JOY_RIGHT][1] = HID_KEY_ARROW_RIGHT;

	uint8_t keycode[6] = {0};
	uint8_t modifier = 0;

	uint8_t esp_modifier = 0;
	uint8_t esp_keycode = 0;
	uint8_t ukey = (uint8_t)key;

	if (alt_pressed && key == '\n')
	{
		esp_modifier = KEYBOARD_MODIFIER_LEFTALT;
		esp_keycode = HID_KEY_ENTER;
	}
	else if (ukey == 0xF2)
	{
		esp_modifier = KEYBOARD_MODIFIER_LEFTSHIFT;
		esp_keycode = HID_KEY_SPACE;
	}
	else if (ukey < 128)
	{
		if (conv_table[ukey][0])
			esp_modifier = KEYBOARD_MODIFIER_LEFTSHIFT;

		esp_keycode = conv_table[ukey][1];
	}

	if (state != KEY_STATE_HOLD && esp_keycode != 0)
		esp_i2c_push_hid(esp_modifier, esp_keycode, (uint8_t)state);

	if (tud_hid_n_ready(USB_ITF_KEYBOARD) &&
		reg_is_bit_set(REG_ID_CF2, CF2_USB_KEYB_ON))
	{
		if (state == KEY_STATE_PRESSED)
		{
			modifier = esp_modifier;
			keycode[0] = esp_keycode;
		}
		else if (state == KEY_STATE_RELEASED)
		{
			modifier = 0;
			memset(keycode, 0, sizeof(keycode));
		}

		if (state != KEY_STATE_HOLD)
			tud_hid_n_keyboard_report(USB_ITF_KEYBOARD, 0, modifier, keycode);
	}
}

static struct key_callback key_callback = { .func = key_cb };

static void touch_cb(int8_t x, int8_t y)
{
	last_key_time_ms = to_ms_since_boot(get_absolute_time());

	if (bkl_auto_off)
	{
		switch (bkl_step)
		{
			case 0: reg_set_value(REG_ID_BKL, 0); break;
			case 1: reg_set_value(REG_ID_BKL, 85); break;
			case 2: reg_set_value(REG_ID_BKL, 170); break;
			case 3: reg_set_value(REG_ID_BKL, 255); break;
		}

		backlight_sync();
		bkl_auto_off = false;
	}

	if (keyboard_get_capslock())
	{
		if (tud_hid_n_ready(USB_ITF_MOUSE))
			tud_hid_n_mouse_report(USB_ITF_MOUSE, 0, 0x00, 0, 0, 0, 0);

		uint8_t keycode[6] = {0};
		uint8_t nav_key = 0;
		uint32_t now_ms = to_ms_since_boot(get_absolute_time());

		if (now_ms < nav_block_until_ms)
			return;

		nav_acc_x += x;
		nav_acc_y += y;

		if (nav_acc_y <= -12)
			nav_key = HID_KEY_ARROW_UP;
		else if (nav_acc_y >= 12)
			nav_key = HID_KEY_ARROW_DOWN;
		else if (nav_acc_x <= -12)
			nav_key = HID_KEY_ARROW_LEFT;
		else if (nav_acc_x >= 12)
			nav_key = HID_KEY_ARROW_RIGHT;
		else
			return;

		nav_acc_x = 0;
		nav_acc_y = 0;
		nav_block_until_ms = now_ms + 350;

		esp_i2c_push_hid(0, nav_key, KEY_STATE_PRESSED);
		esp_i2c_push_hid(0, nav_key, KEY_STATE_RELEASED);

		if (tud_hid_n_ready(USB_ITF_KEYBOARD) &&
			reg_is_bit_set(REG_ID_CF2, CF2_USB_KEYB_ON))
		{
			keycode[0] = nav_key;

			tud_hid_n_keyboard_report(USB_ITF_KEYBOARD, 0, 0, keycode);

			nav_release_pending = true;
			nav_release_time_ms = now_ms + 30;
		}

		return;
	}

	nav_acc_x = 0;
	nav_acc_y = 0;
	nav_block_until_ms = 0;

	self.mouse_moved = true;

	esp_i2c_push_mouse(x, y, self.mouse_btn);

	if (tud_hid_n_ready(USB_ITF_MOUSE) &&
		reg_is_bit_set(REG_ID_CF2, CF2_USB_MOUSE_ON))
	{
		tud_hid_n_mouse_report(USB_ITF_MOUSE, 0, self.mouse_btn, x, y, 0, 0);
	}
}
static struct touch_callback touch_callback = { .func = touch_cb };

uint16_t tud_hid_get_report_cb(uint8_t itf, uint8_t report_id, hid_report_type_t report_type, uint8_t *buffer, uint16_t reqlen)
{
	(void)itf;
	(void)report_id;
	(void)report_type;
	(void)buffer;
	(void)reqlen;

	return 0;
}

void tud_hid_set_report_cb(uint8_t itf, uint8_t report_id, hid_report_type_t report_type, uint8_t const *buffer, uint16_t len)
{
	(void)itf;
	(void)report_id;
	(void)report_type;
	(void)buffer;
	(void)len;
}

void tud_vendor_rx_cb(uint8_t itf)
{
	uint8_t buff[64] = { 0 };
	tud_vendor_n_read(itf, buff, 64);

	reg_process_packet(buff[0], buff[1], self.write_buffer, &self.write_len);

	tud_vendor_n_write(itf, self.write_buffer, self.write_len);
}

void tud_mount_cb(void)
{

}

mutex_t *usb_get_mutex(void)
{
	return &self.mutex;
}

void usb_init(void)
{
	tusb_init();

	esp_i2c_init();

	keyboard_add_key_callback(&key_callback);
	touchpad_add_touch_callback(&touch_callback);

	irq_set_exclusive_handler(USB_LOW_PRIORITY_IRQ, low_priority_worker_irq);
	irq_set_enabled(USB_LOW_PRIORITY_IRQ, true);

	mutex_init(&self.mutex);
	add_alarm_in_us(USB_TASK_INTERVAL_US, timer_task, NULL, true);
}
