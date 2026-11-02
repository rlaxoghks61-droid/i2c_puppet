# nice!nano v2 - BBQ20KBD RP2040 SWD programmer

USB CMSIS-DAP v1 programmer from Free-DAP (BSD-3-Clause). Not a BLE keyboard bridge.
Original keyboard branch NRF52 is unchanged.

## Physical GPIOs (unchanged)
- I2C: P0.29 INT, P0.04 SDA, P0.05 SCL, unused by this programmer
- SWCLK: P1.14, SWDIO: P1.13 (swapped in firmware after measured reversed output at RP2040 pads)
- Target must have power and common ground

NOTE: The published standard nice!nano v2 variant assigns Arduino D8=P1.04 and D9=P1.06.
This code intentionally uses physical P1.13/P1.14, roles swapped in firmware, not Arduino D8/D9 digital aliases.
P1.14 availability on the physical nice!nano board is unverified. It must be exposed physically for SWD to work.

## Compile configuration
- nRFMicro-like Boards 1.0.2, nice!nano v2
- Target bootloader reported S140 6.1.1
- Explicit linker nrf52840_s140_v6.ld, application starts 0x26000
- UF2 family 0xADA52840

## Install
Download NICENANO_V2_CMSIS_DAP.uf2 from GitHub Actions successful artifact,
double-click RESET, and copy it to NICENANO drive. Program will replace BLE keyboard firmware.
After reboot, the device must enumerate as nice!nano v2 CMSIS-DAP.

## Test SWD with xPack OpenOCD (Windows)
"C:\Users\com\Downloads\OpenOCD\xpack-openocd-0.12.0-7\bin\openocd.exe" -f interface/cmsis-dap.cfg -f target/rp2040.cfg -c "adapter speed 100" -c "init; targets; shutdown"

Build validation does not prove hardware success. Do not flash an incompatible SoftDevice layout.

## 2026-11-02 SWCLK/SWDIO correction
Measured at RP2040: when software sent CLK=0 IO=1, the observed voltages were CLK=3.3V IO=0V; when CLK=1 IO=0, observed CLK=0V IO=3.3V. Therefore only physical GPIO roles have been swapped: P1.14 is now SWCLK, P1.13 is now SWDIO. This is a hypothesis to test with OpenOCD; no wires moved. The original I2C GPIO connections remain untouched.
