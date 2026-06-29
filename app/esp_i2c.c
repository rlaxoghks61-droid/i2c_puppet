#include "esp_i2c.h"

#define QUEUE_SIZE 32

struct key_event_packet
{
	uint8_t key;
	uint8_t state;
};

static volatile struct key_event_packet queue[QUEUE_SIZE];
static volatile uint8_t head = 0;
static volatile uint8_t tail = 0;

void esp_i2c_push_key(char key, enum key_state state)
{
	uint8_t next = (head + 1) % QUEUE_SIZE;

	if (next == tail)
		return;

	queue[head].key = (uint8_t)key;
	queue[head].state = (uint8_t)state;

	head = next;
}

void esp_i2c_pop_key(uint8_t *buffer, uint8_t *len)
{
	if (tail != head)
	{
		buffer[0] = queue[tail].key;
		buffer[1] = queue[tail].state;
		tail = (tail + 1) % QUEUE_SIZE;
	}
	else
	{
		buffer[0] = 0;
		buffer[1] = 0;
	}

	*len = 2;
}
