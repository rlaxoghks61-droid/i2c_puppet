#include "esp_i2c.h"

#define QUEUE_SIZE 32

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

static void push_event(uint8_t type, uint8_t a, uint8_t b, uint8_t c)
{
	uint8_t next = (head + 1) % QUEUE_SIZE;
	if (next == tail)
		return;

	queue[head].type = type;
	queue[head].a = a;
	queue[head].b = b;
	queue[head].c = c;
	head = next;
}

void esp_i2c_push_hid(uint8_t modifier, uint8_t keycode, uint8_t state)
{
	push_event(ESP_EVT_KEY, modifier, keycode, state);
}

void esp_i2c_push_mouse(int8_t x, int8_t y, uint8_t buttons)
{
	push_event(ESP_EVT_MOUSE, (uint8_t)x, (uint8_t)y, buttons);
}

void esp_i2c_pop_key(uint8_t *buffer, uint8_t *len)
{
	if (tail != head)
	{
		buffer[0] = queue[tail].type;
		buffer[1] = queue[tail].a;
		buffer[2] = queue[tail].b;
		buffer[3] = queue[tail].c;
		tail = (tail + 1) % QUEUE_SIZE;
	}
	else
	{
		buffer[0] = 0;
		buffer[1] = 0;
		buffer[2] = 0;
		buffer[3] = 0;
	}

	*len = 4;
}
