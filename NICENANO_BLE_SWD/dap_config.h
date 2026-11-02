// Free-DAP platform adapter for nice!nano v2 nRF52840.
// P1.12 SWCLK ; P1.14 SWDIO (bidirectional), fixed GPIO indices.
// External RP2040 powered separately/shared common 3.3 V rail and GND.
// License of dap.c/dap.h: BSD-3-Clause. This adapter: MIT.
#ifndef XIAO_NRF52840_DAP_CONFIG_H
#define XIAO_NRF52840_DAP_CONFIG_H

#include <stdint.h>
#include <nrf.h>

#define DAP_CONFIG_DEFAULT_PORT        DAP_PORT_SWD
#define DAP_CONFIG_DEFAULT_CLOCK       100000
#define DAP_CONFIG_PACKET_SIZE         64
#define DAP_CONFIG_PACKET_COUNT        1
#define DAP_CONFIG_JTAG_DEV_COUNT      1
#define DAP_CONFIG_VENDOR_STR          "Open CMSIS-DAP"
#define DAP_CONFIG_PRODUCT_STR         "nice!nano v2 CMSIS-DAP"
#define DAP_CONFIG_SER_NUM_STR         "NICENANO-SWD-01"
#define DAP_CONFIG_CMSIS_DAP_VER_STR   "1.3.0"
#define DAP_CONFIG_FW_VER_STR          "0.1.2"

// Calibrating timing in software is hardware/core dependent: deliberately slow.
#define DAP_CONFIG_PERFORMANCE_ATTR
#define DAP_CONFIG_DELAY_CONSTANT      5000
#define DAP_CONFIG_FAST_CLOCK          20000000

#define SWCLK_MASK (1UL << 12) // SWCLK physical = nRF P1.12
#define SWDIO_MASK (1UL << 14) // SWDIO physical = nRF P1.14

static inline void DAP_CONFIG_SWCLK_TCK_write(int v) { if(v) NRF_P1->OUTSET=SWCLK_MASK; else NRF_P1->OUTCLR=SWCLK_MASK; }
static inline void DAP_CONFIG_SWDIO_TMS_write(int v) { if(v) NRF_P1->OUTSET=SWDIO_MASK; else NRF_P1->OUTCLR=SWDIO_MASK; }
static inline void DAP_CONFIG_TDI_write(int v) { (void)v; }
static inline void DAP_CONFIG_TDO_write(int v) { (void)v; }
static inline void DAP_CONFIG_nTRST_write(int v) { (void)v; }
static inline void DAP_CONFIG_nRESET_write(int v) { (void)v; } // target reset not wired

static inline int DAP_CONFIG_SWCLK_TCK_read(void) { return !!(NRF_P1->IN & SWCLK_MASK); }
static inline int DAP_CONFIG_SWDIO_TMS_read(void) { return !!(NRF_P1->IN & SWDIO_MASK); }
static inline int DAP_CONFIG_TDI_read(void) { return 0; }
static inline int DAP_CONFIG_TDO_read(void) { return 0; }
static inline int DAP_CONFIG_nTRST_read(void) { return 1; }
static inline int DAP_CONFIG_nRESET_read(void) { return 1; }

static inline void DAP_CONFIG_SWCLK_TCK_set(void) { NRF_P1->OUTSET=SWCLK_MASK; }
static inline void DAP_CONFIG_SWCLK_TCK_clr(void) { NRF_P1->OUTCLR=SWCLK_MASK; }
static inline void DAP_CONFIG_SWDIO_TMS_in(void) { NRF_P1->DIRCLR=SWDIO_MASK; }
static inline void DAP_CONFIG_SWDIO_TMS_out(void) {
  NRF_P1->OUTSET = SWDIO_MASK; // safe before enabling output driver
  NRF_P1->DIRSET = SWDIO_MASK;
}

static inline void DAP_CONFIG_SETUP(void) {
  NRF_P1->DIRCLR = SWCLK_MASK | SWDIO_MASK;
  NRF_P1->PIN_CNF[12] = (GPIO_PIN_CNF_INPUT_Connect << GPIO_PIN_CNF_INPUT_Pos);
  NRF_P1->PIN_CNF[14] = (GPIO_PIN_CNF_INPUT_Connect << GPIO_PIN_CNF_INPUT_Pos);
  NRF_P1->OUTSET = SWCLK_MASK | SWDIO_MASK;
}
static inline void DAP_CONFIG_DISCONNECT(void) {
  NRF_P1->DIRCLR = SWCLK_MASK | SWDIO_MASK;
}
static inline void DAP_CONFIG_CONNECT_SWD(void) {
  NRF_P1->OUTSET = SWCLK_MASK | SWDIO_MASK;
  NRF_P1->DIRSET = SWCLK_MASK;
  DAP_CONFIG_SWDIO_TMS_out();
}
static inline void DAP_CONFIG_CONNECT_JTAG(void) { DAP_CONFIG_CONNECT_SWD(); }
static inline void DAP_CONFIG_LED(int index, int state) { (void)index; (void)state; }

// 64-MHz Cortex-M4; approximate delay only. Use adapter speed <= 100 kHz initially.
static inline void DAP_CONFIG_DELAY(uint32_t cycles) {
  while (cycles--) { __NOP(); }
}
#endif
