
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
 * Serial diagnostic: 115200 baud
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


#define I2C_HZ 100000
#define TX_INTERVAL_MS 8
#define IDLE_I2C_POLL_MS 12
#define DIAG_INTERVAL_MS 1000

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

static void configurePhysicalI2CPin(uint8_t physicalPin) {
  NRF_P0->PIN_CNF[physicalPin] =
      (GPIO_PIN_CNF_DIR_Input << GPIO_PIN_CNF_DIR_Pos) |
      (GPIO_PIN_CNF_INPUT_Connect << GPIO_PIN_CNF_INPUT_Pos) |
      (GPIO_PIN_CNF_PULL_Pullup << GPIO_PIN_CNF_PULL_Pos) |
      (GPIO_PIN_CNF_DRIVE_S0D1 << GPIO_PIN_CNF_DRIVE_Pos) |
      (GPIO_PIN_CNF_SENSE_Disabled << GPIO_PIN_CNF_SENSE_Pos);
}

static const uint8_t SDA_GPIO=BBQ_PHYS_SDA, SCL_GPIO=BBQ_PHYS_SCL, INT_GPIO=BBQ_PHYS_INT;
static const uint8_t I2C_ADDR=BBQ_ADDR;
static const uint32_t I2C_TIMEOUT_US=20000UL;
static volatile bool swdActive=false;
static uint32_t twimErrors=0, twimTimeouts=0, twimOkay=0, twimIdleFails=0;
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

// Keep nRF completely passive on both I2C lines (no TWIM signal generation).

// Diagnostic errors:
// 0=success, 2=address NACK, 3=data NACK, 4=other ERROR,
// -20=bus not idle, -21=transaction timed out, -22=short RX.
static int readRegBounded(uint8_t reg, uint8_t *buf, uint8_t len) {
  if (!pinHigh(SDA_GPIO) || !pinHigh(SCL_GPIO)) {
    twimIdleFails++;
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
      twimTimeouts++;
      t->TASKS_STOP=1;
      uint32_t startStop=micros();
      while(!t->EVENTS_STOPPED && (uint32_t)(micros()-startStop)<2000UL) {}
    }
    startTWIM(); // Reset TWIM peripheral state before next probe.
    twimErrors++;
  } else twimOkay++;
  return status;
}


static void beginBBQI2C() {
  startTWIM();
  configurePhysicalInputPullup(BBQ_PHYS_INT);
}

static uint8_t readBBQINT() {
  return (NRF_P1->IN & (1UL << BBQ_PHYS_INT)) ? HIGH : LOW;
}


// CMSIS-DAP v1 USB HID, independent of Bluetooth LE HID.
static uint8_t const cmsis_dap_descriptor[] = {
  0x06,0x00,0xFF,0x09,0x01,0xA1,0x01,
  0x15,0x00,0x26,0xFF,0x00,0x75,0x08,0x95,0x40,
  0x09,0x01,0x81,0x02,0x95,0x40,0x09,0x01,0x91,0x02,0xC0
};
Adafruit_USBD_HID cmsis_hid;
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
  TinyUSBDevice.setManufacturerDescriptor("Open CMSIS-DAP");
  TinyUSBDevice.setProductDescriptor("BBQ20 BLE + CMSIS-DAP");
  cmsis_hid.setReportDescriptor(cmsis_dap_descriptor,sizeof(cmsis_dap_descriptor));
  cmsis_hid.setStringDescriptor("CMSIS-DAP");
  cmsis_hid.setPollInterval(1);
  cmsis_hid.enableOutEndpoint(true);
  cmsis_hid.setReportCallback(nullptr,dapReceive);
  cmsis_hid.begin();
  dap_init();
  if(TinyUSBDevice.mounted()) {
    TinyUSBDevice.detach();
    delay(50);
    TinyUSBDevice.attach();
  }
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

volatile bool irqPending = true; // polling also works without attachInterrupt

uint8_t mods = 0;
uint8_t keys[6] = {0};
uint8_t keyMods[6] = {0};

uint8_t mouseButtons = 0;
uint16_t consumerUsage = 0;

int16_t mouseRemX = 0;
int16_t mouseRemY = 0;

int32_t pendingX = 0;
int32_t pendingY = 0;

bool keyboardDirty = true;
bool mouseDirty = true;
bool consumerDirty = true;

uint32_t lastTx = 0;
uint32_t lastI2CPoll = 0;
bool verbosePackets = false;
uint32_t lastDiag = 0;
uint32_t languageReleaseAt = 0;

bool languageCombo = false;

// RP2040 sends navigation arrow press and release back-to-back.
// Queue each stroke so the BLE host receives distinct HID reports.
static constexpr uint8_t NAV_Q_SIZE = 16;
uint8_t navQueue[NAV_Q_SIZE] = {0};
uint8_t navHead = 0, navTail = 0;
bool navPulsePressed = false;
uint32_t navPulsesSent = 0;
uint32_t navQueueOverflow = 0;

static bool isArrowKey(uint8_t key) {
  return key >= 0x4F && key <= 0x52; // Right, Left, Down, Up
}
static void queueNavKey(uint8_t key) {
  const uint8_t next = (uint8_t)((navHead + 1) % NAV_Q_SIZE);
  if (next == navTail) { navQueueOverflow++; return; }
  navQueue[navHead] = key;
  navHead = next;
}


// Diagnostic counters
uint32_t reg70Ok = 0;
uint32_t reg70Fail = 0;
uint32_t packetCount = 0;

void onBbqInt() {
  irqPending = true;
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
}

int8_t scaledAxis(int8_t raw, int16_t &rem) {
  int16_t v = int16_t(raw); // 1.0x raw touchpad motion
  int16_t out = v;

  rem = 0;

  return (int8_t)constrain(out, -127, 127);
}


bool readPacket(uint8_t p[4]) {
  int err=readRegBounded(BBQ_EVENT_REG,p,4);
  if(err!=0) {
    reg70Fail++;
    static uint32_t logAt=0;
    if(millis()-logAt >= 1000) {
      Serial.print("REG70_ERR=");Serial.print(err);
      Serial.print(" SDA=");Serial.print(pinHigh(BBQ_PHYS_SDA));
      Serial.print(" SCL=");Serial.println(pinHigh(BBQ_PHYS_SCL));
      logAt=millis();
    }
    return false;
  }
  reg70Ok++;
  return true;
}

void printPacket(uint8_t p[4]) {
  Serial.print("PACKET: ");

  for (int i = 0; i < 4; i++) {
    if (p[i] < 0x10) Serial.print("0");
    Serial.print(p[i], HEX);
    Serial.print(" ");
  }

  Serial.println();
}

void serviceInput() {
  // Diagnostic mode:
  // Poll regardless of INT state.
  noInterrupts();
  irqPending = false;
  interrupts();

  for (int i = 0; i < 16; i++) {
    uint8_t p[4];

    if (!readPacket(p)) {
      irqPending = true;
      break;
    }

    if (p[0] == 0) {
      break;
    }

    packetCount++;
    if (verbosePackets) printPacket(p);

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

    if (i == 15) {
      irqPending = true;
    }
  }

  if (readBBQINT() == LOW) {
    irqPending = true;
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


void printDiagnostics() {
  if(millis()-lastDiag<DIAG_INTERVAL_MS) return;
  lastDiag=millis();
  Serial.print("BLE=");Serial.print(Bluefruit.connected()?1:0);
  Serial.print(" SWD=");Serial.print(swdActive?1:0);
  Serial.print(" REG70_OK=");Serial.print(reg70Ok);
  Serial.print(" REG70_FAIL=");Serial.print(reg70Fail);
  Serial.print(" PACKETS=");Serial.print(packetCount);
  Serial.print(" NAV_SENT=");Serial.print(navPulsesSent);
  Serial.print(" NAV_Q=");Serial.print((navHead+NAV_Q_SIZE-navTail)%NAV_Q_SIZE);
  Serial.print(" NAV_OVF=");Serial.print(navQueueOverflow);
  Serial.print(" TWIM_OK=");Serial.print(twimOkay);
  Serial.print(" TWIM_TIMEOUT=");Serial.print(twimTimeouts);
  Serial.print(" SDA=");Serial.print(pinHigh(BBQ_PHYS_SDA));
  Serial.print(" SCL=");Serial.print(pinHigh(BBQ_PHYS_SCL));
  Serial.print(" INT=");Serial.println(readBBQINT());
}
void setup() {
  startCMSIS();
  Serial.begin(115200);
  uint32_t usbStart=millis();
  while(!Serial && (uint32_t)(millis()-usbStart)<1500UL) delay(10);

  beginBBQI2C();
  Serial.print("Physical GPIO SDA=P0."); Serial.print(BBQ_PHYS_SDA);
  Serial.print(" SCL=P1."); Serial.print(BBQ_PHYS_SCL);
  Serial.print(" INT=P1."); Serial.println(BBQ_PHYS_INT);
  Serial.print("TWIM0 PSEL SDA="); Serial.print(NRF_TWIM0->PSEL.SDA);
  Serial.print(" SCL="); Serial.println(NRF_TWIM0->PSEL.SCL);

  Serial.println("BBQ20KBD I2C diagnostic started");

  Bluefruit.begin();
  Bluefruit.setName("BBQ20KBD Bridge");
  Bluefruit.setTxPower(-4);

  bledis.setManufacturer("BBQ20KBD");
  bledis.setModel("nice!nano v2");
  bledis.begin();

  blehid.begin();

  startAdv();

  Serial.println("BLE HID + USB CMSIS-DAP initialized");
  Serial.println("FAST_MOUSE: 1.0x motion, 8ms TX, packet logging off (send v to toggle)");
  Serial.println("I2C SDA=P0.04 SCL=P1.15 INT=P1.13; SWDIO=P1.14 SWCLK=P1.12");
}

void loop() {
  serviceCMSIS();
  // Drain queued packets immediately when INT is asserted; fall back to a bounded periodic poll.
  if (!swdActive && (readBBQINT() == LOW || (uint32_t)(millis()-lastI2CPoll) >= IDLE_I2C_POLL_MS)) {
    lastI2CPoll = millis();
    serviceInput();
  }
  printDiagnostics();
  while (Serial.available()) {
    const char c = (char)Serial.read();
    if (c == 'v' || c == 'V') { verbosePackets = !verbosePackets; Serial.print("PACKET_LOG="); Serial.println(verbosePackets?1:0); }
  }

  if (!Bluefruit.connected()) {
    keyboardDirty = true;
    mouseDirty = true;
    consumerDirty = true;

    pendingX = 0;
    pendingY = 0;
    navTail = navHead;
    navPulsePressed = false;

    delay(2);
    return;
  }

  uint32_t now = millis();

  if (now - lastTx < TX_INTERVAL_MS) {
    delay(1);
    return;
  }

  // Emit independent BLE reports for arrow press and release.
  if (navPulsePressed) {
    if (blehid.keyboardReport(effectiveMods(), keys)) {
      navPulsePressed = false;
      navTail = (uint8_t)((navTail + 1) % NAV_Q_SIZE);
      navPulsesSent++;
      keyboardDirty = false;
      lastTx = now;
    }
    return;
  }
  if (navTail != navHead) {
    uint8_t navKeys[6] = { navQueue[navTail], 0, 0, 0, 0, 0 };
    if (blehid.keyboardReport(effectiveMods(), navKeys)) {
      navPulsePressed = true;
      lastTx = now;
    }
    return;
  }

  if (languageCombo) {
    uint8_t languageKeys[6] = {0x2C,0,0,0,0,0};

    if ((int32_t)(now - languageReleaseAt) < 0) {
      if (blehid.keyboardReport(0x02, languageKeys)) {
        lastTx = now;
      }
      return;
    }

    languageCombo = false;
    keyboardDirty = true;
  }

  if (keyboardDirty) {
    if (blehid.keyboardReport(effectiveMods(), keys)) {
      keyboardDirty = false;
      lastTx = now;
    }
    return;
  }

  if (consumerDirty) {
    if (blehid.consumerReport(consumerUsage)) {
      consumerDirty = false;
      lastTx = now;
    }
    return;
  }

  if (mouseDirty) {
    int8_t dx = (int8_t)constrain(
      pendingX, -127, 127
    );

    int8_t dy = (int8_t)constrain(
      pendingY, -127, 127
    );

    if (blehid.mouseReport(
      mouseButtons, dx, dy, 0, 0
    )) {
      pendingX -= dx;
      pendingY -= dy;

      mouseDirty = (
        pendingX != 0 || pendingY != 0
      );

      lastTx = now;
    }
  }
}