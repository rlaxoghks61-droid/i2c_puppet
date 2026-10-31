# NRF52 — BBQ20KBD RP2040 firmware compatibility branch

This branch contains **BBQ20KBD (RP2040)** firmware changes for use with
**Seeed XIAO nRF52840** as an external I2C host. It is **not** an Arduino
sketch for the nRF52840 board.

## Protocol (unchanged)
- 7-bit I2C slave address: `0x1F`
- Version register: `0x01` (version value `0x11`)
- Event register: `0x70` (4-byte packets: keyboard/mouse/consumer)
- Host pins: nRF D3=P0.29 INT, D4=P0.04 SDA, D5=P0.05 SCL
- Target pins (RP2040): GP0 INT, GP28 SDA, GP29 SCL

## Modified BBQ20KBD files
- `app/main.c`: replace invalid GPIO 0xFF IRQ registration with a valid pin; install callback before any pin IRQ is enabled.
- `app/puppet_i2c.c`: handle I2C START/STOP/ABORT, drain RX FIFO, avoid unbounded TX FIFO writes in the IRQ, enable slave address matching after IRQ handler setup.
- `app/interrupt.c`: restore queued-event INT state after legacy 1ms interrupt pulses.
- `app/esp_i2c.c` and `app/esp_i2c.h`: expose interrupt-state synchronization.
All other files inherited from `main` unchanged.

## Build
Build this branch with the pinned Raspberry Pi Pico SDK:
```bash
git submodule update --init
cd 3rdparty/pico-sdk && git submodule update --init && cd ../..
cmake -S . -B build -DPICO_BOARD=bbq20kbd_breakout -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j4
```
The UF2 is typically output as `build/app/i2c_puppet.uf2`.

## Verification status
Code review against the repository's pinned Pico SDK and prior mock-header syntax/unit tests.
The new branch code has **not** been compiled with the complete real SDK nor run on physical
boards. It is not proof of resolved address NACK. The original `main` remains unmodified.
