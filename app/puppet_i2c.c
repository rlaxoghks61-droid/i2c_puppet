#include "esp_i2c.h"
#include "puppet_i2c.h"

#include "reg.h"

#include <hardware/i2c.h>
#include <hardware/irq.h>
#include <pico/stdlib.h>

#define REG_ID_INVALID		0x00

static i2c_inst_t *i2c_instances[2] = { i2c0, i2c1 };

static struct
{
	i2c_inst_t *i2c;

	struct
	{
		uint8_t reg;
		uint8_t data;
	} read_buffer;

	uint8_t write_buffer[4];
	uint8_t write_len;
} self;

// Receive one register byte without discarding another byte in the RX FIFO.
static void receive_byte(uint8_t value)
{
    if (self.read_buffer.reg == REG_ID_INVALID)
    {
        self.read_buffer.reg = value;
        if (value & PACKET_WRITE_MASK)
            return; // Wait for the register's data byte.

        if (value == REG_ID_ESP_KEY_EVENT)
        {
            esp_i2c_pop_key(self.write_buffer, &self.write_len);
            self.read_buffer.reg = REG_ID_INVALID;
            return;
        }
    }
    else
    {
        self.read_buffer.data = value;
    }

    reg_process_packet(self.read_buffer.reg, self.read_buffer.data,
                       self.write_buffer, &self.write_len);
    self.read_buffer.reg = REG_ID_INVALID;
}

static void irq_handler(void)
{
    i2c_hw_t *hw = self.i2c->hw;
    const uint32_t status = hw->intr_stat;

    if (status & I2C_IC_INTR_MASK_M_TX_ABRT_BITS)
    {
        (void)hw->clr_tx_abrt;
        self.read_buffer.reg = REG_ID_INVALID;
        self.write_len = 0;
    }

    if (status & I2C_IC_INTR_MASK_M_START_DET_BITS)
    {
        (void)hw->clr_start_det;
        self.read_buffer.reg = REG_ID_INVALID;
    }

    if (status & I2C_IC_INTR_MASK_M_STOP_DET_BITS)
    {
        (void)hw->clr_stop_det;
        self.read_buffer.reg = REG_ID_INVALID;
    }

    if (status & I2C_IC_INTR_MASK_M_RX_FULL_BITS)
    {
        while (i2c_get_read_available(self.i2c))
            receive_byte((uint8_t)(self.i2c->hw->data_cmd & 0xff));
    }

    if (status & I2C_IC_INTR_MASK_M_RD_REQ_BITS)
    {
        // FIFO writes in interrupt context must never wait indefinitely.
        uint8_t n = self.write_len;
        if (n > sizeof(self.write_buffer)) n = sizeof(self.write_buffer);
        if (n == 0)
        {
            if (i2c_get_write_available(self.i2c))
                self.i2c->hw->data_cmd = 0;
        }
        else
        {
            for (uint8_t i = 0; i < n && i2c_get_write_available(self.i2c); ++i)
                self.i2c->hw->data_cmd = self.write_buffer[i];
        }

        // Match the Pico SDK ordering: service RD_REQ, THEN clear it.
        (void)hw->clr_rd_req;
    }
}

void puppet_i2c_sync_address(void)
{
	i2c_set_slave_mode(self.i2c, true, reg_get_value(REG_ID_ADR));
}

void puppet_i2c_init(void)
{
	self.i2c = i2c_instances[(PIN_PUPPET_SCL / 2) % 2];

	i2c_init(self.i2c, 100 * 1000);

	gpio_set_function(PIN_PUPPET_SDA, GPIO_FUNC_I2C);
	gpio_pull_up(PIN_PUPPET_SDA);

	gpio_set_function(PIN_PUPPET_SCL, GPIO_FUNC_I2C);
	gpio_pull_up(PIN_PUPPET_SCL);

	self.i2c->hw->intr_mask =
		I2C_IC_INTR_MASK_M_RD_REQ_BITS |
		I2C_IC_INTR_MASK_M_RX_FULL_BITS |
		I2C_IC_INTR_MASK_M_TX_ABRT_BITS |
		I2C_IC_INTR_MASK_M_STOP_DET_BITS |
		I2C_IC_INTR_MASK_M_START_DET_BITS;

	const int irq = I2C0_IRQ + i2c_hw_index(self.i2c);

	irq_set_exclusive_handler(irq, irq_handler);
	irq_set_enabled(irq, true);

	// Only enable slave address matching once the interrupt handler is ready.
	puppet_i2c_sync_address();
}
