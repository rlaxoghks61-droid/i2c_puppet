# BBQ20KBD RP2040 i2c_puppet keyboard communication diagnostic (nice!nano v2)

Board: nRFMicro:nrf52:nicenanov2 S140 6.1.1
Pin map: INT D3=P0.29 SDA D4=P0.04 SCL D5=P0.05
I2C 0x1F, 100kHz, direct NRF_TWIM0 PSEL routing.
RP2040 protocol (repo's NRF52 branch):
  0x01 1 byte firmware version (VER=0x11 for 1.1)
  0x04 1 byte fifo_count
  0x09 2 byte pressed/held/released and ASCII keycode
Also tests custom 0x70 4-byte HID events (from app/esp_i2c.h of NRF52 branch).
The installed RP2040 image supports BOTH: official FIFO 0x09/2-byte and custom HID FIFO 0x70/4-byte.

Flashing the UF2 replaces the nRF SWD programmer temporarily.
Double reset to the nice!nano bootloader, drag UF2 to disk.
Open Arduino IDE Serial Monitor to the nice!nano *normal-mode* port at 115200.
KEY lines appear on key presses. STATUS ACK=1 and FW=0x11 expected for stock
i2c_puppet 1.1. No keyboard BLE HID in this image; this isolates I2C functionality.
