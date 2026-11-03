
/*
 * BBQ20KBD custom firmware -> nRF52840 BLE HID Bridge
 *
 * Board: nice!nano v2 / nRF52840 compatible
 * SDA: physical D4 / P0.04
 * SCL: physical D10 / P1.15
 * INT: physical D8 / P1.13
 * I2C address: 0x1F
 * Event register: 0x70
 *
 */

#include <Arduino.h>
#include <nrf.h>
#include <string.h>
extern "C" {
#include "dap.h"
}
#include <Adafruit_TinyUSB.h>
#include <bluefruit.h>

#define BBQ_ADDR 0x1F
#define BBQ_EVENT_REG 0x70


#define KEYBOARD_INTERVAL_MS 8
#define MOUSE_USB_INTERVAL_MS 8
#define IDLE_I2C_POLL_MS 12

// User-confirmed actual GPIO mapping: SDA=P0.04, SCL=P1.15, INT=P1.13.
// Nordic TWIM PSEL needs port bit 5 for P1.xx pins (0x20 | pin).
static constexpr uint8_t BBQ_PHYS_SDA = 4;
static constexpr uint8_t BBQ_PHYS_SCL = 15;
static constexpr uint8_t BBQ_PHYS_INT = 13;
static constexpr uint32_t BBQ_SCL_PSEL = 0x20UL | BBQ_PHYS_SCL;

static void configurePhysicalInputPullup(uint8_t physicalPin) {
  NRF_P1->PIN_CNF[physicalPin] =
      (GPIO_PIN_CNF_DIR_Input << GPIO_PIN_CNF_DIR_Pos) |
      (GPIO_PIN_CNF_INPUT_Connect << GPIO_PIN_CNF_INPUT_Pos) |
      (GPIO_PIN_CNF_PULL_Pullup << GPIO_PIN_CNF_PULL_Pos) |
      (GPIO_PIN_CNF_DRIVE_S0S1 << GPIO_PIN_CNF_DRIVE_Pos) |
      (GPIO_PIN_CNF_SENSE_Disabled << GPIO_PIN_CNF_SENSE_Pos);
}

static const uint8_t SDA_GPIO=BBQ_PHYS_SDA, SCL_GPIO=BBQ_PHYS_SCL;
static const uint8_t I2C_ADDR=BBQ_ADDR;
static const uint32_t I2C_TIMEOUT_US=20000UL;
static volatile bool swdActive=false;
static bool pinHigh(uint8_t p) { return p==SCL_GPIO ? !!(NRF_P1->IN & (1UL << p)) : !!(NRF_P0->IN & (1UL << p)); }

static void configureI2CPin(uint8_t p) {
  (p==SCL_GPIO ? NRF_P1 : NRF_P0)->PIN_CNF[p] =
      (GPIO_PIN_CNF_DIR_Input << GPIO_PIN_CNF_DIR_Pos) |
      (GPIO_PIN_CNF_INPUT_Connect << GPIO_PIN_CNF_INPUT_Pos) |
      (GPIO_PIN_CNF_PULL_Pullup << GPIO_PIN_CNF_PULL_Pos) |
      (GPIO_PIN_CNF_DRIVE_S0D1 << GPIO_PIN_CNF_DRIVE_Pos) |
      (GPIO_PIN_CNF_SENSE_Disabled << GPIO_PIN_CNF_SENSE_Pos);
}

static void startTWIM() {
  NRF_TWIM0->ENABLE=0;
  configureI2CPin(SDA_GPIO);
  configureI2CPin(SCL_GPIO);
  NRF_TWIM0->PSEL.SDA = SDA_GPIO; 
  NRF_TWIM0->PSEL.SCL = BBQ_SCL_PSEL;
  NRF_TWIM0->FREQUENCY = TWIM_FREQUENCY_FREQUENCY_K100;
  NRF_TWIM0->SHORTS = TWIM_SHORTS_LASTTX_STARTRX_Msk | TWIM_SHORTS_LASTRX_STOP_Msk;
  NRF_TWIM0->ENABLE = (TWIM_ENABLE_ENABLE_Enabled << TWIM_ENABLE_ENABLE_Pos);
}

// Timeout recovery must reset the TWIM peripheral before the next read.
static int readRegBounded(uint8_t reg, uint8_t *buf, uint8_t len) {
  if (!pinHigh(SDA_GPIO) || !pinHigh(SCL_GPIO)) {
    return -20;
  }
  NRF_TWIM_Type *t = NRF_TWIM0;
  uint8_t txByte = reg; // EasyDMA buffer in RAM; valid until STOPPED.
  t->EVENTS_STOPPED=0;
  t->EVENTS_ERROR=0;
  t->EVENTS_TXSTARTED=0;
  t->EVENTS_RXSTARTED=0;
  t->EVENTS_LASTTX=0;
  t->EVENTS_LASTRX=0;
  t->ERRORSRC=TWIM_ERRORSRC_ANACK_Msk | TWIM_ERRORSRC_DNACK_Msk | TWIM_ERRORSRC_OVERRUN_Msk;
  t->ADDRESS=I2C_ADDR;
  t->TXD.PTR=(uint32_t)&txByte;
  t->TXD.MAXCNT=1;
  t->RXD.PTR=(uint32_t)buf;
  t->RXD.MAXCNT=len;
  t->SHORTS = TWIM_SHORTS_LASTTX_STARTRX_Msk | TWIM_SHORTS_LASTRX_STOP_Msk;
  t->TASKS_RESUME=1;
  t->TASKS_STARTTX=1;

  bool errorStopIssued=false;
  const uint32_t started=micros();
  while (!t->EVENTS_STOPPED) {
    if (t->EVENTS_ERROR && !errorStopIssued) {
      t->TASKS_STOP=1;
      errorStopIssued=true;
    }
    if ((uint32_t)(micros()-started) >= I2C_TIMEOUT_US) break;
  }

  uint32_t errSrc=t->ERRORSRC;
  const bool stopped=(t->EVENTS_STOPPED!=0);
  const uint32_t amount=t->RXD.AMOUNT;
  int status=0;
  if (!stopped) status=-21;
  else if (errSrc & TWIM_ERRORSRC_ANACK_Msk) status=2;
  else if (errSrc & TWIM_ERRORSRC_DNACK_Msk) status=3;
  else if (errSrc) status=4;
  else if (amount != len) status=-22;
  if (status != 0) {
    if (status==-21) {
      t->TASKS_STOP=1;
      uint32_t startStop=micros();
      while(!t->EVENTS_STOPPED && (uint32_t)(micros()-startStop)<2000UL) {}
    }
    startTWIM(); // Reset TWIM peripheral state before next probe.
  }
  return status;
}


static void beginBBQI2C() {
  startTWIM();
  configurePhysicalInputPullup(BBQ_PHYS_INT);
}

static uint8_t readBBQINT() {
  return (NRF_P1->IN & (1UL << BBQ_PHYS_INT)) ? HIGH : LOW;
}


// CMSIS-DAP v1 USB HID, separate from the keyboard/mouse HID interface.
static uint8_t const cmsis_dap_descriptor[] = {
  0x06,0x00,0xFF,0x09,0x01,0xA1,0x01,
  0x15,0x00,0x26,0xFF,0x00,0x75,0x08,0x95,0x40,
  0x09,0x01,0x81,0x02,0x95,0x40,0x09,0x01,0x91,0x02,0xC0
};
Adafruit_USBD_HID cmsis_hid;

enum { USB_RID_KEYBOARD = 1, USB_RID_MOUSE = 2, USB_RID_CONSUMER = 3 };
static uint8_t const usb_input_descriptor[] = {
  TUD_HID_REPORT_DESC_KEYBOARD(HID_REPORT_ID(USB_RID_KEYBOARD)),
  TUD_HID_REPORT_DESC_MOUSE(HID_REPORT_ID(USB_RID_MOUSE)),
  TUD_HID_REPORT_DESC_CONSUMER(HID_REPORT_ID(USB_RID_CONSUMER))
};
Adafruit_USBD_HID usb_input_hid;

static uint8_t req_buf[64];
static uint8_t resp_buf[64];
static volatile bool req_pending=false;
static volatile bool resp_pending=false;

static void dapReceive(uint8_t report_id, hid_report_type_t report_type,
                       uint8_t const* buf, uint16_t len) {
  (void)report_type;
  if(report_id!=0 || !buf || len==0 || req_pending || resp_pending) return;
  if(len>sizeof(req_buf)) len=sizeof(req_buf);
  memcpy(req_buf,buf,len);
  if(len<sizeof(req_buf)) memset(req_buf+len,0,sizeof(req_buf)-len);
  req_pending=true;
}

static void startCMSIS() {
  // Rebuild USB without CDC; keep both the input HID and CMSIS-DAP HID.
  TinyUSBDevice.detach();
  Serial.end();
  delay(30);
  TinyUSBDevice.setManufacturerDescriptor("BBQ20KBD");
  TinyUSBDevice.setProductDescriptor("BBQ20 Keyboard Mouse + DAP");

  usb_input_hid.setReportDescriptor(usb_input_descriptor, sizeof(usb_input_descriptor));
  usb_input_hid.setStringDescriptor("BBQ20 Keyboard Mouse Consumer");
  usb_input_hid.setPollInterval(2);
  usb_input_hid.begin();

  cmsis_hid.setReportDescriptor(cmsis_dap_descriptor,sizeof(cmsis_dap_descriptor));
  cmsis_hid.setStringDescriptor("CMSIS-DAP");
  cmsis_hid.setPollInterval(1);
  cmsis_hid.enableOutEndpoint(true);
  cmsis_hid.setReportCallback(nullptr,dapReceive);
  cmsis_hid.begin();
  dap_init();

  TinyUSBDevice.attach();
}

static void serviceCMSIS() {
#ifdef TINYUSB_NEED_POLLING_TASK
  TinyUSBDevice.task();
#endif
  if(req_pending && !resp_pending) {
    uint8_t cmd=req_buf[0];
    if(cmd==0x02) swdActive=true;
    memset(resp_buf,0,sizeof(resp_buf));
    int n=dap_process_request(req_buf,sizeof(req_buf),resp_buf,sizeof(resp_buf));
    if(n<0) { memset(resp_buf,0,sizeof(resp_buf)); resp_buf[0]=0xFF; }
    if(cmd==0x03) swdActive=false;
    req_pending=false;
    resp_pending=true;
  }
  if(resp_pending && cmsis_hid.ready()) {
    if(cmsis_hid.sendReport(0,resp_buf,sizeof(resp_buf)))
      resp_pending=false;
  }
}

BLEDis bledis;
BLEHidAdafruit blehid;

// Balanced BLE HID profile: requested 11.25-15ms, slave latency 2,
// 3s supervision timeout. The central (phone/PC) decides actual parameters.
static constexpr uint16_t BLE_PROFILE_MIN_UNITS = 9;
static constexpr uint16_t BLE_PROFILE_MAX_UNITS = 12;
static constexpr uint16_t BLE_PROFILE_SLAVE_LATENCY = 2;
static constexpr uint16_t BLE_PROFILE_TIMEOUT_UNITS = 300; // 3s in 10ms units
static bool wasBleConnected = false;
static uint32_t bleConnectedAt = 0;
static bool bleParamAttempted = false;
static uint32_t lastPhysicalInputMs = 0;

static void serviceBalancedBleProfile() {
  const bool connected = (Bluefruit.connected() != 0);
  if (connected != wasBleConnected) {
    wasBleConnected = connected;
    bleConnectedAt = millis();
    bleParamAttempted = false;
  }
  // Do not renegotiate BLE parameters during the first seconds of typing.
  // The advertised 11.25-15ms / latency 2 profile is still applied immediately.
  if (!connected || bleParamAttempted || (uint32_t)(millis()-bleConnectedAt) < 8000UL ||
      (uint32_t)(millis()-lastPhysicalInputMs) < 1500UL) return;

  BLEConnection* conn = Bluefruit.Connection(Bluefruit.connHandle());
  if (!conn || !conn->connected()) return;

  const uint16_t currentInterval = conn->getConnectionInterval();
  const uint16_t currentLatency = conn->getSlaveLatency();
  if (currentInterval >= BLE_PROFILE_MIN_UNITS &&
      currentInterval <= BLE_PROFILE_MAX_UNITS &&
      currentLatency == BLE_PROFILE_SLAVE_LATENCY) {
    bleParamAttempted = true;
    return;
  }
  // Once per BLE connection; do not spam parameter updates and disrupt HID.
  // Preserve an already-low-latency interval if inside the preferred range.
  const uint16_t targetInterval =
      (currentInterval >= BLE_PROFILE_MIN_UNITS &&
       currentInterval <= BLE_PROFILE_MAX_UNITS) ? currentInterval : BLE_PROFILE_MAX_UNITS;
  bleParamAttempted = true;
  conn->requestConnectionParameter(
      targetInterval, BLE_PROFILE_SLAVE_LATENCY, BLE_PROFILE_TIMEOUT_UNITS);
}


// USB has priority whenever a USB host has enumerated this device.
// BLE remains connected but does not receive keyboard/mouse/consumer events
// while USB is active. CMSIS-DAP is independent.
static bool useUsbInput() {
  return TinyUSBDevice.mounted() && usb_input_hid.isValid();
}
static bool inputAvailable() {
  return useUsbInput() || Bluefruit.connected();
}
static bool sendKeyboardToHost(uint8_t mod, uint8_t key[6]) {
  if (useUsbInput()) return usb_input_hid.ready() &&
         usb_input_hid.keyboardReport(USB_RID_KEYBOARD, mod, key);
  return Bluefruit.connected() && blehid.keyboardReport(mod, key);
}
static bool sendMouseToHost(uint8_t buttons, int8_t dx, int8_t dy) {
  if (useUsbInput()) return usb_input_hid.ready() &&
         usb_input_hid.mouseReport(USB_RID_MOUSE,buttons,dx,dy,0,0);
  return Bluefruit.connected() && blehid.mouseReport(buttons,dx,dy,0,0);
}
static bool sendConsumerToHost(uint16_t usage) {
  if (useUsbInput()) return usb_input_hid.ready() &&
         usb_input_hid.sendReport16(USB_RID_CONSUMER,usage);
  return Bluefruit.connected() && blehid.consumerReport(usage);
}


uint8_t mods = 0;
uint8_t keys[6] = {0};
uint8_t keyMods[6] = {0};

// Preserve every keyboard state change, as in the working ESP32 bridge.
// Otherwise press+release processed in one I2C batch collapses into no key event.
static constexpr uint8_t KEY_FRAME_CAP = 32;
struct KeyFrame {
  uint8_t mod;
  uint8_t key[6];
};
KeyFrame keyFrames[KEY_FRAME_CAP];
uint8_t keyFrameHead=0, keyFrameTail=0, keyFrameCount=0;
bool keyboardResyncNeeded=true;


uint8_t mouseButtons = 0;
uint16_t consumerUsage = 0;

int16_t mouseRemX = 0;
int16_t mouseRemY = 0;

int32_t pendingX = 0;
int32_t pendingY = 0;

bool keyboardDirty = true;
bool mouseDirty = true;
bool consumerDirty = true;

// Independent pacing for keyboard, mouse and consumer reports.
uint32_t lastKeyboardTx = 0;
uint32_t lastMouseTx = 0;
uint32_t lastConsumerTx = 0;
bool keyboardTxSent = false;
bool mouseTxSent = false;
bool consumerTxSent = false;
uint8_t nextHidLane = 0;
uint32_t lastI2CPoll = 0;
uint32_t languageReleaseAt = 0;

bool languageCombo = false;

// On a USB/BLE handover, synchronize the active host with the actual state,
// not a backlog of HID reports prepared for the old connection.
enum OutputRoute : uint8_t { ROUTE_NONE=0, ROUTE_BLE=1, ROUTE_USB=2 };
OutputRoute activeRoute=ROUTE_NONE;
bool routeReleasePending = false;
bool releaseOldBleKeyboard=false, releaseOldBleMouse=false, releaseOldBleConsumer=false;



// RP2040 sends navigation arrow press and release back-to-back.
// Queue each stroke so the BLE host receives distinct HID reports.
static constexpr uint8_t NAV_Q_SIZE = 16;
uint8_t navQueue[NAV_Q_SIZE] = {0};
uint8_t navHead = 0, navTail = 0;
bool navPulsePressed = false;
static constexpr uint32_t NAV_COOLDOWN_MS = 350;
uint32_t lastNavAcceptedMs = 0;
bool navAcceptedOnce = false;

static void updateOutputRoute() {
  const OutputRoute next = useUsbInput() ? ROUTE_USB :
                           (Bluefruit.connected() ? ROUTE_BLE : ROUTE_NONE);
  if (next==activeRoute) return;

  if (activeRoute==ROUTE_BLE && next==ROUTE_USB && Bluefruit.connected()) {
    releaseOldBleKeyboard=true;
    releaseOldBleMouse=true;
    releaseOldBleConsumer=true;
  }
  if (next!=ROUTE_USB) {
    releaseOldBleKeyboard=false;
    releaseOldBleMouse=false;
    releaseOldBleConsumer=false;
  }
  activeRoute=next;
  // Both transports use delta events. A missing release on an old connection
  // must NEVER seed a new USB/BLE host with a stuck key.
  mods=0;
  memset(keys,0,sizeof(keys));
  memset(keyMods,0,sizeof(keyMods));
  mouseButtons=0;
  consumerUsage=0;
  routeReleasePending=(next!=ROUTE_NONE);
  // Never replay old key taps, directional strokes or mouse motion on a new host.
  keyFrameHead=keyFrameTail=keyFrameCount=0;
  navTail=navHead;
  navPulsePressed=false;
  languageCombo=false;
  pendingX=pendingY=0;
  keyboardDirty=true;
  keyboardResyncNeeded=true;
  mouseDirty=true;
  consumerDirty=true;
  lastKeyboardTx=lastMouseTx=lastConsumerTx=0;
  keyboardTxSent=mouseTxSent=consumerTxSent=false;
  nextHidLane=0;
}

static void releaseBleHostOnUsbSwitch() {
  if (!useUsbInput() || !Bluefruit.connected()) return;
  if (releaseOldBleKeyboard) {
    uint8_t emptyKeys[6]={0};
    if (blehid.keyboardReport(0,emptyKeys)) releaseOldBleKeyboard=false;
  } else if (releaseOldBleMouse) {
    if (blehid.mouseReport(uint8_t(0),int8_t(0),int8_t(0),int8_t(0),int8_t(0))) releaseOldBleMouse=false;
  } else if (releaseOldBleConsumer) {
    if (blehid.consumerReport(0)) releaseOldBleConsumer=false;
  }
}


static bool isArrowKey(uint8_t key) {
  return key >= 0x4F && key <= 0x52; // Right, Left, Down, Up
}
static void queueNavKey(uint8_t key) {
  const uint32_t now = millis();
  // No backlog: a long touch must not become a burst of queued arrows.
  if ((navAcceptedOnce && (uint32_t)(now - lastNavAcceptedMs) < NAV_COOLDOWN_MS) ||
      navPulsePressed || navHead != navTail) {
    return;
  }
  const uint8_t next = (uint8_t)((navHead + 1) % NAV_Q_SIZE);
  navQueue[navHead] = key;
  navHead = next;
  lastNavAcceptedMs = now;
  navAcceptedOnce = true;
}


uint8_t effectiveMods() {
  uint8_t m = mods;

  for (uint8_t i = 0; i < 6; i++) {
    if (keys[i]) {
      m |= keyMods[i];
    }
  }

  return m;
}

void enqueueCurrentKeyFrame() {
  KeyFrame frame;
  frame.mod = effectiveMods();
  memcpy(frame.key, keys, 6);
  if (keyFrameCount) {
    const uint8_t prev = (uint8_t)((keyFrameTail + KEY_FRAME_CAP - 1) % KEY_FRAME_CAP);
    if (keyFrames[prev].mod == frame.mod && memcmp(keyFrames[prev].key,frame.key,6)==0) return;
  }
  if (keyFrameCount == KEY_FRAME_CAP) {
    // Avoid sending a stale backlog; recover from current physical state.
    keyFrameHead = keyFrameTail = keyFrameCount = 0;
    keyboardResyncNeeded = true;
  }
  keyFrames[keyFrameTail] = frame;
  keyFrameTail = (uint8_t)((keyFrameTail+1) % KEY_FRAME_CAP);
  keyFrameCount++;
}

void updateKey(uint8_t mod, uint8_t key, uint8_t state) {
  if (state != 1 && state != 3) return;

  if (isArrowKey(key)) {
    if (state == 1) queueNavKey(key);
    return;
  }

  if ((mod & 0x22) && key == 0x2C) {
    if (state == 1) {
      languageCombo = true;
      languageReleaseAt = millis() + 80;
    }
    return;
  }

  if (key == 0) {
    if (state == 1) {
      mods |= mod;
    } else {
      mods &= ~mod;
    }
  } else {
    int slot = -1;

    for (int i = 0; i < 6; i++) {
      if (keys[i] == key) {
        slot = i;
        break;
      }
    }

    if (state == 1) {
      if (slot < 0) {
        for (int i = 0; i < 6; i++) {
          if (keys[i] == 0) {
            slot = i;
            break;
          }
        }
      }

      if (slot >= 0) {
        keys[slot] = key;
        keyMods[slot] = mod;
      }
    } else if (slot >= 0) {
      keys[slot] = 0;
      keyMods[slot] = 0;
    }
  }

  keyboardDirty = true;
  enqueueCurrentKeyFrame();
}

int8_t scaledAxis(int8_t raw, int16_t &rem) {
  int16_t v = int16_t(raw) * 9 + rem; // 0.9x raw touchpad motion
  int16_t out = v / 10;
  rem = v - out * 10;

  return (int8_t)constrain(out, -127, 127);
}


bool readPacket(uint8_t p[4]) {
  return readRegBounded(BBQ_EVENT_REG, p, 4) == 0;
}

void serviceInput() {
  for (int i = 0; i < 16; i++) {
    uint8_t p[4];

    if (!readPacket(p)) {
      break;
    }

    if (p[0] == 0) {
      break;
    }

    lastPhysicalInputMs=millis();

    if (p[0] == 1) {
      updateKey(p[1], p[2], p[3]);
    }
    else if (p[0] == 2) {
      pendingX += scaledAxis(
        (int8_t)p[1],
        mouseRemX
      );

      pendingY += scaledAxis(
        (int8_t)p[2],
        mouseRemY
      );

      pendingX = constrain(pendingX, -32767, 32767);
      pendingY = constrain(pendingY, -32767, 32767);

      mouseButtons = p[3] & 0x07;
      mouseDirty = true;
    }
    else if (p[0] == 3) {
      consumerUsage = (p[3] == 1)
        ? (uint16_t(p[1]) | (uint16_t(p[2]) << 8))
        : 0;

      consumerDirty = true;
    }

  }
}

void startAdv() {
  Bluefruit.Advertising.stop();

  Bluefruit.Advertising.addFlags(
    BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE
  );

  Bluefruit.Advertising.addTxPower();

  Bluefruit.Advertising.addAppearance(
    BLE_APPEARANCE_HID_KEYBOARD
  );

  Bluefruit.Advertising.addService(blehid);
  Bluefruit.Advertising.addName();

  Bluefruit.Advertising.restartOnDisconnect(true);
  Bluefruit.Advertising.setInterval(32, 244);
  Bluefruit.Advertising.setFastTimeout(30);
  Bluefruit.Advertising.start(0);
}


void setup() {
  startCMSIS();
  beginBBQI2C();

  // Three SoftDevice notification slots; HID reports share one BLE radio.
  Bluefruit.configPrphConn(BLE_GATT_ATT_MTU_DEFAULT, 6, 3,
                            BLE_GATTC_WRITE_CMD_TX_QUEUE_SIZE_DEFAULT);
  Bluefruit.begin();
  Bluefruit.setName("BBQ20KBD Bridge");
  Bluefruit.setTxPower(-4);

  bledis.setManufacturer("BBQ20KBD");
  bledis.setModel("nice!nano v2");
  bledis.begin();

  blehid.begin();
  // Set connection parameters after BLE HID initialization.
  Bluefruit.Periph.setConnInterval(BLE_PROFILE_MIN_UNITS, BLE_PROFILE_MAX_UNITS);
  Bluefruit.Periph.setConnSlaveLatency(BLE_PROFILE_SLAVE_LATENCY);
  Bluefruit.Periph.setConnSupervisionTimeoutMS(3000);

  startAdv();

}

static uint32_t negotiatedBleIntervalMs() {
  BLEConnection* conn = Bluefruit.Connection(Bluefruit.connHandle());
  if (!conn || !conn->connected()) return 15;
  const uint16_t units = conn->getConnectionInterval();
  if (units < 6 || units > 3200) return 15;
  return ((uint32_t)units * 5UL + 3UL) / 4UL;
}

// No fixed report-count cap. The keyboard and mouse are paced separately
// according to the negotiated BLE interval and the transport in use.
static uint32_t keyboardPacingMs() {
  if (activeRoute != ROUTE_BLE) return KEYBOARD_INTERVAL_MS;
  const uint32_t halfInterval=(negotiatedBleIntervalMs()+1UL)/2UL;
  return halfInterval>KEYBOARD_INTERVAL_MS ? halfInterval : KEYBOARD_INTERVAL_MS;
}

static void onReportSent(uint32_t now,uint32_t& lastSent,bool& hasSent) {
  lastSent=now;
  hasSent=true;
}
static bool laneDue(uint32_t now,uint32_t lastSent,bool hasSent,uint32_t interval) {
  return !hasSent || (uint32_t)(now-lastSent)>=interval;
}

static bool serviceKeyboardLane(uint32_t now) {
  if (!laneDue(now,lastKeyboardTx,keyboardTxSent,keyboardPacingMs())) return false;
  if (routeReleasePending) {
    uint8_t emptyKeys[6]={0};
    if (sendKeyboardToHost(0,emptyKeys)) {
      routeReleasePending=false;
      onReportSent(millis(),lastKeyboardTx,keyboardTxSent);
    }
    return true;
  }
  if (navPulsePressed) {
    if (sendKeyboardToHost(effectiveMods(),keys)) {
      navPulsePressed=false;
      navTail=(uint8_t)((navTail+1)%NAV_Q_SIZE);
      keyboardResyncNeeded=true;
      onReportSent(millis(),lastKeyboardTx,keyboardTxSent);
    }
    return true;
  }
  if (navTail!=navHead) {
    uint8_t navKeys[6]={navQueue[navTail],0,0,0,0,0};
    if (sendKeyboardToHost(effectiveMods(),navKeys)) {
      navPulsePressed=true;
      onReportSent(millis(),lastKeyboardTx,keyboardTxSent);
    }
    return true;
  }
  if (languageCombo) {
    uint8_t languageKeys[6]={0x2C,0,0,0,0,0};
    if ((int32_t)(now-languageReleaseAt)<0) {
      if (sendKeyboardToHost(0x02,languageKeys))
        onReportSent(millis(),lastKeyboardTx,keyboardTxSent);
      return true;
    }
    languageCombo=false;
    keyboardDirty=true;
  }
  if (keyFrameCount) {
    KeyFrame &frame=keyFrames[keyFrameHead];
    if (sendKeyboardToHost(frame.mod,frame.key)) {
      keyFrameHead=(uint8_t)((keyFrameHead+1)%KEY_FRAME_CAP);
      keyFrameCount--;
      keyboardDirty=false;
      keyboardResyncNeeded=false;
      onReportSent(millis(),lastKeyboardTx,keyboardTxSent);
    }
    return true;
  }
  if (keyboardResyncNeeded || keyboardDirty) {
    if (sendKeyboardToHost(effectiveMods(),keys)) {
      keyboardResyncNeeded=false;
      keyboardDirty=false;
      onReportSent(millis(),lastKeyboardTx,keyboardTxSent);
    }
    return true;
  }
  return false;
}

static bool serviceMouseLane(uint32_t now) {
  if (!mouseDirty) return false;
  const uint32_t interval=(activeRoute==ROUTE_BLE)
      ? negotiatedBleIntervalMs() : MOUSE_USB_INTERVAL_MS;
  if (!laneDue(now,lastMouseTx,mouseTxSent,interval)) return false;
  const int8_t dx=(int8_t)constrain(pendingX,-127,127);
  const int8_t dy=(int8_t)constrain(pendingY,-127,127);
  if (sendMouseToHost(mouseButtons,dx,dy)) {
    pendingX-=dx;
    pendingY-=dy;
    mouseDirty=(pendingX!=0 || pendingY!=0);
    onReportSent(millis(),lastMouseTx,mouseTxSent);
  }
  return true;
}

static bool serviceConsumerLane(uint32_t now) {
  if (!consumerDirty ||
      !laneDue(now,lastConsumerTx,consumerTxSent,keyboardPacingMs())) return false;
  if (sendConsumerToHost(consumerUsage)) {
    consumerDirty=false;
    onReportSent(millis(),lastConsumerTx,consumerTxSent);
  }
  return true;
}

void loop() {
  serviceCMSIS();
  serviceBalancedBleProfile();
  updateOutputRoute();
  releaseBleHostOnUsbSwitch();
  if (!swdActive && (readBBQINT()==LOW ||
      (uint32_t)(millis()-lastI2CPoll)>=IDLE_I2C_POLL_MS)) {
    lastI2CPoll=millis();
    serviceInput();
  }
  if (!inputAvailable()) {
    keyboardDirty=true;
    mouseDirty=true;
    consumerDirty=true;
    pendingX=pendingY=0;
    navTail=navHead;
    navPulsePressed=false;
    keyFrameHead=keyFrameTail=keyFrameCount=0;
    keyboardResyncNeeded=true;
    delay(2);
    return;
  }
  const uint32_t now=millis();
  for(uint8_t offset=0;offset<3;++offset) {
    const uint8_t lane=(uint8_t)((nextHidLane+offset)%3);
    const bool attempted=(lane==0) ? serviceKeyboardLane(now) :
                         (lane==1) ? serviceMouseLane(now) : serviceConsumerLane(now);
    // One BLE notification attempt per pass avoids chained 100ms waits;
    // USB lanes may run independently during the same pass.
    if(activeRoute==ROUTE_BLE && attempted) {
      nextHidLane=(uint8_t)((lane+1)%3);
      delay(1);
      return;
    }
  }
  nextHidLane=(uint8_t)((nextHidLane+1)%3);
  delay(1);
}
