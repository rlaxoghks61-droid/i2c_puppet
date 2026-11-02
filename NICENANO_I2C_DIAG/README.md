# Nice!nano v2 BBQ20 I2C diagnostics V2

V1 printed READY then could hang forever in Adafruit Wire_nRF52.cpp
(endTransmission() / requestFrom() have indefinite waits on events).

V2 runs hardware NRF_TWIM0 EasyDMA directly with 20ms timeout, a
bounded STOP recovery, heartbeat before the first probe, and prints
SDA/SCL/INT physical logic levels, TWIM event bits and error source.

Physical GPIO fixed: SDA P0.04 (D4), SCL P0.05 (D5), INT P0.29 (D3).
RP2040 7-bit address 0x1F. Version 0x01 (1 byte), key count 0x04,
custom HID events 0x70 (4 bytes). Leave SWD D8/D9 unchanged.

Open USB serial monitor 115200 after installing UF2. If no I2C ACK
the code keeps printing HEARTBEAT and REG01_VER result statuses:
-20 bus not idle, -21 TWIM completion timeout, 2 address NACK,
3 data NACK, 4 hardware ERRORSRC, -22 short response. 0 = success.

This is a diagnostic image, no BLE HID, not the final keyboard firmware.
Do not test OpenOCD while this UF2 is installed; it temporarily replaces
the SWD CMSIS-DAP probe on nice!nano.


## V3 passive bus ownership test

At boot, disable TWIM0 and disconnect its PSEL mapping, configure SDA=P0.04
and SCL=P0.05 as inputs with pull-ups. **No I2C transfers or clock pulses**.
Serial monitor 115200 prints PASSIVE_START then PASSIVE every second.
If SDA stays LOW in this state, the disabled TWIM0 is not actively driving it;
identify an external low driver or other electrical issue before reflashing.
When SDA is HIGH, send the letter 'a' in Serial Monitor to enable TWIM and
run bounded I2C probes. Send 'd' to disable TWIM again.
Pins and RP2040 firmware unchanged.
