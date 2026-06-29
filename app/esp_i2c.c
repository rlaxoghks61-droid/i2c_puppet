#include "esp_i2c.h"

#define QUEUE_SIZE 32

struct hid_event_packet
{
	uint8_t modifier;
	uint8_t keycode;
	uint8_t state;
};

static volatile struct hid_event_packet queue[QUEUE_SIZE];
static volatile uint8_t head = 0;
static volatile uint8_t tail = 0;

void esp_i2c_push_hid(uint8_t modifier, uint8_t keycode, uint8_t state)
{
	uint8_t next = (head + 1) % QUEUE_SIZE;

	if (next == tail)
		return;

	queue[head].modifier = modifier;
	queue[head].keycode = keycode;
	queue[head].state = state;

	head = next;
}

void esp_i2c_pop_key(uint8_t *buffer, uint8_t *len)
{
	if (tail != head)
	{
		buffer[0] = queue[tail].modifier;
		buffer[1] = queue[tail].keycode;
		buffer[2] = queue[tail].state;

		tail = (tail + 1) % QUEUE_SIZE;
	}
	else
	{
		buffer[0] = 0;
		buffer[1] = 0;
		buffer[2] = 0;
	}

	*len = 3;
}
