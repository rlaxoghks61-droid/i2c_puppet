#include "esp_i2c.h"

#include <hardware/i2c.h>
#include <hardware/irq.h>
#include <pico/stdlib.h>

#define ESP_I2C_PORT i2c0
#define ESP_I2C_ADDR 0x42

#define ESP_I2C_SDA_PIN 28
#define ESP_I2C_SCL_PIN 29

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

static void i2c0_irq_handler(void)
{
	uint32_t status = i2c_get_hw(ESP_I2C_PORT)->intr_stat;

	if (status & I2C_IC_INTR_STAT_R_RD_REQ_BITS)
	{
		uint8_t key = 0;
		uint8_t state = 0;

		if (tail != head)
		{
			key = queue[tail].key;
			state = queue[tail].state;
			tail = (tail + 1) % QUEUE_SIZE;
		}

		i2c_get_hw(ESP_I2C_PORT)->data_cmd = key;
		i2c_get_hw(ESP_I2C_PORT)->data_cmd = state;

		(void)i2c_get_hw(ESP_I2C_PORT)->clr_rd_req;
	}
}

void esp_i2c_init(void)
{
	i2c_init(ESP_I2C_PORT, 100 * 1000);

	gpio_set_function(ESP_I2C_SDA_PIN, GPIO_FUNC_I2C);
	gpio_set_function(ESP_I2C_SCL_PIN, GPIO_FUNC_I2C);

	gpio_pull_up(ESP_I2C_SDA_PIN);
	gpio_pull_up(ESP_I2C_SCL_PIN);

	i2c_set_slave_mode(ESP_I2C_PORT, true, ESP_I2C_ADDR);

	irq_set_exclusive_handler(I2C0_IRQ, i2c0_irq_handler);
	irq_set_enabled(I2C0_IRQ, true);

	i2c_get_hw(ESP_I2C_PORT)->intr_mask = I2C_IC_INTR_MASK_M_RD_REQ_BITS;
}
