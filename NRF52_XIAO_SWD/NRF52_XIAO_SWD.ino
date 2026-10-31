// XIAO nRF52840 CMSIS-DAP v1 (USB HID) SWD debug probe.
// See README_KO.md. D8=P1.13 -> RP2040 SWCLK, D9=P1.14 -> RP2040 SWDIO.
// Free-DAP BSD-3-Clause code is included as dap.c + dap.h.
#include <Arduino.h>
#include <Adafruit_TinyUSB.h>
#include <string.h>
extern "C" {
#include "dap.h"
}

// CMSIS-DAP v1 HID interface: 64-byte input and output, no Report ID.
// (Vendor-defined Usage Page, Application Collection, one IN and one OUT report)
static uint8_t const cmsis_dap_report_descriptor[] = {
  0x06, 0x00, 0xFF, // USAGE_PAGE(Vendor-defined)
  0x09, 0x01,       // USAGE(1)
  0xA1, 0x01,       // COLLECTION(Application)
  0x15, 0x00,       // LOGICAL_MINIMUM(0)
  0x26, 0xFF, 0x00, // LOGICAL_MAXIMUM(255)
  0x75, 0x08,       // REPORT_SIZE(8)
  0x95, 0x40,       // REPORT_COUNT(64)
  0x09, 0x01,       // USAGE(1)
  0x81, 0x02,       // INPUT(Data,Var,Abs)
  0x95, 0x40,       // REPORT_COUNT(64)
  0x09, 0x01,       // USAGE(1)
  0x91, 0x02,       // OUTPUT(Data,Var,Abs)
  0xC0              // END_COLLECTION
};

Adafruit_USBD_HID cmsis_hid;
static uint8_t request_buf[64];
static uint8_t response_buf[64];
static volatile bool request_pending = false;
static volatile bool response_pending = false;

// TinyUSB callback (USB context). Do not perform SWD operations in USB ISR.
static void cmsis_set_report(uint8_t report_id, hid_report_type_t report_type,
                             uint8_t const *buf, uint16_t len)
{
  (void)report_type;
  if (report_id != 0 || buf == nullptr || len == 0 || request_pending || response_pending)
    return;
  if (len > sizeof(request_buf))
    len = sizeof(request_buf);
  memcpy(request_buf, buf, len);
  if (len < sizeof(request_buf))
    memset(request_buf + len, 0, sizeof(request_buf) - len);
  request_pending = true;
}

void setup()
{
  // Set descriptor before starting the USB interface.
  TinyUSBDevice.setManufacturerDescriptor("Open CMSIS-DAP");
  TinyUSBDevice.setProductDescriptor("XIAO nRF52840 CMSIS-DAP");
  cmsis_hid.setReportDescriptor(cmsis_dap_report_descriptor,
                               sizeof(cmsis_dap_report_descriptor));
  cmsis_hid.setStringDescriptor("CMSIS-DAP");
  cmsis_hid.setPollInterval(1);
  cmsis_hid.enableOutEndpoint(true);
  cmsis_hid.setReportCallback(nullptr, cmsis_set_report);
  cmsis_hid.begin();
  dap_init();

  if (TinyUSBDevice.mounted()) {
    TinyUSBDevice.detach();
    delay(50);
    TinyUSBDevice.attach();
  }
}

void loop()
{
#ifdef TINYUSB_NEED_POLLING_TASK
  TinyUSBDevice.task();
#endif

  // Only process after a fully copied request has arrived.
  if (request_pending && !response_pending)
  {
    memset(response_buf, 0, sizeof(response_buf));
    int n = dap_process_request(request_buf, sizeof(request_buf),
                                response_buf, sizeof(response_buf));
    if (n < 0) {
      memset(response_buf, 0, sizeof(response_buf));
      response_buf[0] = 0xFF; // invalid DAP command
    }
    request_pending = false;
    response_pending = true;
  }

  if (response_pending && cmsis_hid.ready())
  {
    if (cmsis_hid.sendReport(0, response_buf, sizeof(response_buf)))
      response_pending = false;
  }
}
