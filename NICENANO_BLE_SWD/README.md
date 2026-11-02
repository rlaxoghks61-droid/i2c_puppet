# Combined BBQ20 BLE HID keyboard / CMSIS-DAP RP2040 flasher for nice!nano v2

Uses native bounded TWIM0 EasyDMA I2C 100 kHz with RP2040 firmware 0x70/4-byte
HID packet protocol at address 0x1F, relays keyboard/mouse/consumer via
Bluefruit BLE HID. Exposes simultaneous USB CMSIS-DAP v1 HID for PC OpenOCD
flashing of RP2040 firmware without replacing nice!nano sketch.

Verified physical assignments unchanged: INT D3=P0.29, SDA D4=P0.04,
SCL D5=P0.05; SWDIO=P1.13, SWCLK=P1.14.
RP2040 is programmed from PC via OpenOCD over USB (not internally by nRF).
DAP Connect pauses I2C; DAP Disconnect resumes I2C. Serial monitor 115200.
The sketch fixes original one-byte language combo key array to 6 bytes.

Built for nRFMicro nice!nano v2 S140 6.1.1 application start 0x26000.
The combined BLE+USB runtime behavior is unverified on hardware until tested.
Preserved Free-DAP BSD-3-Clause dap.c/h implementation.
