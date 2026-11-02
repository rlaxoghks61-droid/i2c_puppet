# nice!nano nRF52840 pin mapper

Arduino nice!nano v2 board selected with SoftDevice S140 6.1.1, UF2 family 0xADA52840.
Program provides a USB CDC command line for testing all D0 to D10 physical labels,
by requesting HIGH (approx. 3.3V) and LOW (approx. 0V) one at a time.

- Default Windows script profile 'xiao' tests the pin layout already supplied by user:
  D0=P0.02 D1=P0.03 D2=P0.28 D3=P0.29 D4=P0.04 D5=P0.05
  D6=P1.11 D7=P1.12 D8=P1.13 D9=P1.14 D10=P1.15.
- '--profile nice' tests nice!nano v2 standard core variant:
  D0=P0.06 D1=P0.08 D2=P0.17 D3=P0.20 D4=P0.22 D5=P0.24
  D6=P1.00 D7=P0.11 D8=P1.04 D9=P1.06 D10=P0.09.

The explicit GPIO maps are intentionally tested separately because the bootloader
name is not enough evidence to establish the physical PCB layout.

SAFETY: Disconnect other electronics from D0..D10 before driving GPIO high/low;
the firmware deliberately does NOT write UICR. nice standard D10=P0.09 is skipped
if the NFC function is active, not forcibly reconfigured. Test outputs auto-release
after 120 seconds. A successful internal GPIO readback does not prove the physical
pin labeled Dn is connected to that GPIO.

This is a temporary diagnostic image: it replaces the currently running
CMSIS-DAP firmware on the nRF until that firmware is re-flashed.
Do not execute OpenOCD while the pin scan is installed.

Windows: py -m pip install pyserial
         py "C:\Users\com\Downloads\NICENANO_D0_D10_PIN_TEST.py"
         py "C:\Users\com\Downloads\NICENANO_D0_D10_PIN_TEST.py" --profile nice
