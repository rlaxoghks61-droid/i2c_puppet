#include "esp_i2c.h"
#include "app_config.h"

#include <pico/stdlib.h>

#define QUEUE_SIZE 32
#define ESP_INT_PIN PIN_INT

struct esp_event_packet
{
	uint8_t type;
	uint8_t a;
	uint8_t b;
	uint8_t c;
};

static volatile struct esp_event_packet queue[QUEUE_SIZE];
static volatile uint8_t head = 0;
static volatile uint8_t tail = 0;

static void update_int_pin(void)
{
	if (head == tail)
		gpio_put(ESP_INT_PIN, 1);  // queue empty
	else
		gpio_put(ESP_INT_PIN, 0);  // event waiting
}

void esp_i2c_init(void)
{
	gpio_init(ESP_INT_PIN);
	gpio_set_dir(ESP_INT_PIN, GPIO_OUT);
	gpio_put(ESP_INT_PIN, 1);
}

static void push_event(uint8_t type, uint8_t a, uint8_t b, uint8_t c)
{
	uint8_t next = (head + 1) % QUEUE_SIZE;

	if (next == tail)
	{
		tail = (tail + 1) % QUEUE_SIZE;
	}

	queue[head].type = type;
	queue[head].a = a;
	queue[head].b = b;
	queue[head].c = c;

	head = next;

	update_int_pin();
}

void esp_i2c_push_hid(uint8_t modifier, uint8_t keycode, uint8_t state)
{
	push_event(
		ESP_EVT_KEY,
		modifier,
		keycode,
		state
	);
}

void esp_i2c_push_mouse(int8_t x, int8_t y, uint8_t buttons)
{
	push_event(
		ESP_EVT_MOUSE,
		(uint8_t)x,
		(uint8_t)y,
		buttons
	);
}

void esp_i2c_push_consumer(uint16_t usage, uint8_t state)
{
	push_event(
		ESP_EVT_CONSUMER,
		(uint8_t)(usage & 0xFF),
		(uint8_t)((usage >> 8) & 0xFF),
		state
	);
}

void esp_i2c_pop_key(uint8_t *buffer, uint8_t *len)
{
	if (tail == head)
	{
		buffer[0] = 0;
		buffer[1] = 0;
		buffer[2] = 0;
		buffer[3] = 0;
		*len = 4;

		update_int_pin();
		return;
	}

	buffer[0] = queue[tail].type;
	buffer[1] = queue[tail].a;
	buffer[2] = queue[tail].b;
	buffer[3] = queue[tail].c;

	tail = (tail + 1) % QUEUE_SIZE;

	*len = 4;

	update_int_pin();
}
