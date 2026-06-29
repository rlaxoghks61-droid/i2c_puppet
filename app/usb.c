#include "esp_i2c.h"
#include "usb.h"

#include "backlight.h"
#include "keyboard.h"
#include "touchpad.h"
#include "reg.h"

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
static uint8_t bkl_step = 3;
static uint32_t last_key_time_ms = 0;
static bool bkl_auto_off = false;

// TODO: What about Ctrl?
// TODO: What should L1, L2, R1, R2 do
// TODO: Should touch send arrow keys as an option?

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
    if (state != KEY_STATE_HOLD)
        esp_i2c_push_hid(KEYBOARD_MODIFIER_LEFTSHIFT, 0, (uint8_t)state);

    return;
}

if (key == KEY_MOD_SHR)
{
    if (state != KEY_STATE_HOLD)
        esp_i2c_push_hid(KEYBOARD_MODIFIER_RIGHTSHIFT, 0, (uint8_t)state);

    return;
}

if (key == KEY_MOD_SYM)
{
    if (state != KEY_STATE_HOLD)
        esp_i2c_push_hid(KEYBOARD_MODIFIER_RIGHTALT, 0, (uint8_t)state);

    return;
}

	if (tud_hid_n_ready(USB_ITF_KEYBOARD) &&
		reg_is_bit_set(REG_ID_CF2, CF2_USB_KEYB_ON))
	{
		if (alt_pressed &&
			key == KEY_BTN_RIGHT2 &&
			state == KEY_STATE_PRESSED)
		{
			bkl_step++;

			if (bkl_step > 3)
				bkl_step = 0;

			switch (bkl_step)
			{
				case 0:
					reg_set_value(REG_ID_BKL, 0);
					break;

				case 1:
					reg_set_value(REG_ID_BKL, 85);
					break;

				case 2:
					reg_set_value(REG_ID_BKL, 170);
					break;

				case 3:
					reg_set_value(REG_ID_BKL, 255);
					break;
			}

			backlight_sync();
			return;
		}

		uint8_t conv_table[128][2] = { HID_ASCII_TO_KEYCODE };
		conv_table['\n'][1] = HID_KEY_ENTER;
		conv_table['\b'][1] = HID_KEY_BACKSPACE;
		conv_table[KEY_JOY_UP][1] = HID_KEY_ARROW_UP;
		conv_table[KEY_JOY_DOWN][1] = HID_KEY_ARROW_DOWN;
		conv_table[KEY_JOY_LEFT][1] = HID_KEY_ARROW_LEFT;
		conv_table[KEY_JOY_RIGHT][1] = HID_KEY_ARROW_RIGHT;

		if (tud_hid_n_ready(USB_ITF_CONSUMER))
		{
			uint16_t consumer_key = 0;

			if (state == KEY_STATE_PRESSED)
			{
				if (key == KEY_BTN_LEFT1)
					consumer_key = 0x00CD;
				else if (key == KEY_BTN_LEFT2)
					consumer_key = HID_USAGE_CONSUMER_AC_HOME;
				else if (key == KEY_BTN_RIGHT1)
					consumer_key = 0x0224;
				else if (key == KEY_BTN_RIGHT2)
					consumer_key = 0x0030;
			}

			tud_hid_n_report(
				USB_ITF_CONSUMER,
				0,
				&consumer_key,
				sizeof(consumer_key)
			);
		}

		uint8_t keycode[6] = {0};
uint8_t modifier = 0;

uint8_t esp_modifier = 0;
uint8_t esp_keycode = 0;

if (alt_pressed && key == '\n')
{
	esp_modifier = KEYBOARD_MODIFIER_LEFTALT;
	esp_keycode = HID_KEY_ENTER;
}
else
{
	if (conv_table[(int)key][0])
		esp_modifier = KEYBOARD_MODIFIER_LEFTSHIFT;

	esp_keycode = conv_table[(int)key][1];

	if (key == 0xF2)
	{
		esp_modifier = KEYBOARD_MODIFIER_LEFTSHIFT;
		esp_keycode = conv_table[0x20][1];
	}
}

if (state != KEY_STATE_HOLD && esp_keycode != 0)
	esp_i2c_push_hid(esp_modifier, esp_keycode, (uint8_t)state);

if (state != KEY_STATE_HOLD)
{
	modifier = esp_modifier;
	keycode[0] = esp_keycode;
}
		
if (state != KEY_STATE_HOLD)
	tud_hid_n_keyboard_report(USB_ITF_KEYBOARD, 0, modifier, keycode);
	}

	if (tud_hid_n_ready(USB_ITF_MOUSE) &&
		reg_is_bit_set(REG_ID_CF2, CF2_USB_MOUSE_ON))
	{
		if (key == KEY_JOY_CENTER)
		{
			if (state == KEY_STATE_PRESSED)
			{
				self.mouse_btn = MOUSE_BUTTON_LEFT;
				self.mouse_moved = false;
				tud_hid_n_mouse_report(USB_ITF_MOUSE, 0, MOUSE_BUTTON_LEFT, 0, 0, 0, 0);
			}
			else if ((state == KEY_STATE_HOLD) && !self.mouse_moved)
			{
				self.mouse_btn = MOUSE_BUTTON_RIGHT;
				tud_hid_n_mouse_report(USB_ITF_MOUSE, 0, MOUSE_BUTTON_RIGHT, 0, 0, 0, 0);
			}
			else if (state == KEY_STATE_RELEASED)
			{
				self.mouse_btn = 0x00;
				tud_hid_n_mouse_report(USB_ITF_MOUSE, 0, 0x00, 0, 0, 0, 0);
			}
		}
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

	if (!tud_hid_n_ready(USB_ITF_KEYBOARD))
		return;

	uint8_t keycode[6] = {0};

		uint32_t now_ms = to_ms_since_boot(get_absolute_time());

if (now_ms < nav_block_until_ms)
	return;
		
		nav_acc_x += x;
		nav_acc_y += y;

		if (nav_acc_y <= -12)
			keycode[0] = HID_KEY_ARROW_UP;
		else if (nav_acc_y >= 12)
			keycode[0] = HID_KEY_ARROW_DOWN;
		else if (nav_acc_x <= -12)
			keycode[0] = HID_KEY_ARROW_LEFT;
		else if (nav_acc_x >= 12)
			keycode[0] = HID_KEY_ARROW_RIGHT;
		else
			return;

		nav_acc_x = 0;
		nav_acc_y = 0;
		nav_block_until_ms = now_ms + 350;

		tud_hid_n_keyboard_report(USB_ITF_KEYBOARD, 0, 0, keycode);

		nav_release_pending = true;
		nav_release_time_ms = now_ms + 30;

		return;
	}

	nav_acc_x = 0;
	nav_acc_y = 0;
	nav_block_until_ms = 0;

	if (!tud_hid_n_ready(USB_ITF_MOUSE) || !reg_is_bit_set(REG_ID_CF2, CF2_USB_MOUSE_ON))
		return;

	self.mouse_moved = true;

	tud_hid_n_mouse_report(USB_ITF_MOUSE, 0, self.mouse_btn, x, y, 0, 0);
}

static struct touch_callback touch_callback = { .func = touch_cb };

uint16_t tud_hid_get_report_cb(uint8_t itf, uint8_t report_id, hid_report_type_t report_type, uint8_t *buffer, uint16_t reqlen)
{
	// TODO not Implemented
	(void)itf;
	(void)report_id;
	(void)report_type;
	(void)buffer;
	(void)reqlen;

	return 0;
}

void tud_hid_set_report_cb(uint8_t itf, uint8_t report_id, hid_report_type_t report_type, uint8_t const *buffer, uint16_t len)
{
	// TODO set LED based on CAPLOCK, NUMLOCK etc...
	(void)itf;
	(void)report_id;
	(void)report_type;
	(void)buffer;
	(void)len;
}

void tud_vendor_rx_cb(uint8_t itf)
{
//	printf("%s: itf: %d, avail: %d\r\n", __func__, itf, tud_vendor_n_available(itf));

	uint8_t buff[64] = { 0 };
	tud_vendor_n_read(itf, buff, 64);
//	printf("%s: %02X %02X %02X\r\n", __func__, buff[0], buff[1], buff[2]);

	reg_process_packet(buff[0], buff[1], self.write_buffer, &self.write_len);

	tud_vendor_n_write(itf, self.write_buffer, self.write_len);
}

void tud_mount_cb(void)
{
	// Send mods over USB by default if USB connected
	reg_set_value(REG_ID_CFG, reg_get_value(REG_ID_CFG) | CFG_REPORT_MODS);
}

mutex_t *usb_get_mutex(void)
{
	return &self.mutex;
}

void usb_init(void)
{
	tusb_init();

	keyboard_add_key_callback(&key_callback);
	touchpad_add_touch_callback(&touch_callback);

	// create a new interrupt that calls tud_task, and trigger that interrupt from a timer
	irq_set_exclusive_handler(USB_LOW_PRIORITY_IRQ, low_priority_worker_irq);
	irq_set_enabled(USB_LOW_PRIORITY_IRQ, true);

	mutex_init(&self.mutex);
	add_alarm_in_us(USB_TASK_INTERVAL_US, timer_task, NULL, true);
}
