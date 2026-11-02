/*
  BBQ20KBD RP2040 / nice!nano v2 I2C Diagnostic V2.
  Native nRF52840 TWIM0 EasyDMA with bounded wait, no Wire busy loops.
  Confirmed physical GPIO mapping:
      INT D8 = P1.13 (input/pullup)
      SDA D3 = P0.20 (I2C open drain/pullup)
      SCL D10 = P0.09 (I2C open drain/pullup)
  RP2040 i2c_puppet firmware ("NRF52" branch): address 0x1F;
  registers 0x01 VER, 0x04 KEY status, 0x09 stock FIFO,
  0x70 4-byte custom HID event FIFO.
  USB serial 115200. No BLE and no SWD programming in this image.
*/
#include <Arduino.h>
#include <Adafruit_TinyUSB.h>
#include <nrf.h>

static const uint8_t SDA_GPIO = 20, SCL_GPIO = 9, INT_GPIO = 13; // INT=P1.13
static const uint8_t I2C_ADDR = 0x1f;
static const uint32_t I2C_TIMEOUT_US = 20000UL;
static const uint32_t SCAN_MS = 1000UL;
static const uint32_t EVENT_MS = 120UL;

static uint32_t readOK=0, readFail=0, evtCount=0, evtEmpty=0, timeouts=0, attempts=0;
static uint32_t lastScan=0, lastEvent=0;
static int lastError=0;
static uint32_t lastErrsrc=0, lastEventBits=0;
static uint8_t version=0, keyStat=0;
static bool validVersion=false;
static bool twimActive=false;

static bool pinHigh(uint8_t p) { return (NRF_P0->IN & (1UL << p)) != 0; }
static bool intHigh() { return (NRF_P1->IN & (1UL << INT_GPIO)) != 0; }

static void configureI2CPin(uint8_t p) {
  NRF_P0->PIN_CNF[p] =
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
  NRF_TWIM0->PSEL.SCL = SCL_GPIO;
  NRF_TWIM0->FREQUENCY = TWIM_FREQUENCY_FREQUENCY_K100;
  NRF_TWIM0->SHORTS = TWIM_SHORTS_LASTTX_STARTRX_Msk | TWIM_SHORTS_LASTRX_STOP_Msk;
  NRF_TWIM0->ENABLE = (TWIM_ENABLE_ENABLE_Enabled << TWIM_ENABLE_ENABLE_Pos);
  twimActive=true;
}

// Keep nRF completely passive on both I2C lines (no TWIM signal generation).
static void releaseTWIM() {
  NRF_TWIM0->ENABLE = (TWIM_ENABLE_ENABLE_Disabled << TWIM_ENABLE_ENABLE_Pos);
  NRF_TWIM0->PSEL.SDA = 0xFFFFFFFFUL;
  NRF_TWIM0->PSEL.SCL = 0xFFFFFFFFUL;
  NRF_P0->DIRCLR = (1UL << SDA_GPIO) | (1UL << SCL_GPIO);
  configureI2CPin(SDA_GPIO);
  configureI2CPin(SCL_GPIO);
  twimActive=false;
  validVersion=false;
}

static void printHex(uint8_t x) {
  if (x < 16) Serial.print('0');
  Serial.print(x, HEX);
}

// Diagnostic errors:
// 0=success, 2=address NACK, 3=data NACK, 4=other ERROR,
// -20=bus not idle, -21=transaction timed out, -22=short RX.
static int readRegBounded(uint8_t reg, uint8_t *buf, uint8_t len) {
  attempts++;
  if (!pinHigh(SDA_GPIO) || !pinHigh(SCL_GPIO)) {
    lastError = -20;
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

  lastEventBits=(t->EVENTS_TXSTARTED?1:0)|
                (t->EVENTS_LASTTX?2:0)|
                (t->EVENTS_RXSTARTED?4:0)|
                (t->EVENTS_LASTRX?8:0)|
                (t->EVENTS_ERROR?16:0)|
                (t->EVENTS_STOPPED?32:0);
  lastErrsrc=t->ERRORSRC;
  const bool stopped=(t->EVENTS_STOPPED!=0);
  const uint32_t amount=t->RXD.AMOUNT;
  int status=0;
  if (!stopped) status=-21;
  else if (lastErrsrc & TWIM_ERRORSRC_ANACK_Msk) status=2;
  else if (lastErrsrc & TWIM_ERRORSRC_DNACK_Msk) status=3;
  else if (lastErrsrc) status=4;
  else if (amount != len) status=-22;
  if (status != 0) {
    if (status==-21) {
      timeouts++;
      t->TASKS_STOP=1;
      uint32_t startStop=micros();
      while(!t->EVENTS_STOPPED && (uint32_t)(micros()-startStop)<2000UL) {}
    }
    startTWIM(); // Reset TWIM peripheral state before next probe.
    readFail++;
  } else readOK++;
  lastError=status;
  return status;
}

static void printBus(const char *label) {
  Serial.print(label);
  Serial.print(" SDA=");Serial.print(pinHigh(SDA_GPIO));
  Serial.print(" SCL=");Serial.print(pinHigh(SCL_GPIO));
  Serial.print(" INT=");Serial.print(intHigh());
  Serial.print(" PSEL_SDA=");Serial.print(NRF_TWIM0->PSEL.SDA);
  Serial.print(" PSEL_SCL=");Serial.println(NRF_TWIM0->PSEL.SCL);
}

static void probe() {
  Serial.print("HEARTBEAT ms="); Serial.print(millis());
  Serial.print(" tries="); Serial.print(attempts);
  Serial.print(" ok="); Serial.print(readOK);
  Serial.print(" fail="); Serial.print(readFail);
  Serial.print(" timeout="); Serial.println(timeouts);
  printBus("BEFORE_REG01");
  uint8_t v=0;
  int err=readRegBounded(0x01,&v,1);
  Serial.print("REG01_VER result=");Serial.print(err);
  Serial.print(" events=0x");Serial.print(lastEventBits,HEX);
  Serial.print(" errorsrc=0x");Serial.print(lastErrsrc,HEX);
  Serial.print(" byte=0x");printHex(v);
  Serial.println();
  if (err==0) {version=v;validVersion=true;}
  else {validVersion=false;printBus("AFTER_FAIL");return;}
  uint8_t count=0;
  err=readRegBounded(0x04,&count,1);
  Serial.print("REG04_KEY result="); Serial.print(err);
  Serial.print(" byte=0x");printHex(count);
  Serial.println();
  if (err==0) keyStat=count;
  printBus("AFTER_PROBE");
}

static void pollEvents() {
  if (!validVersion) return;
  for (uint8_t i=0;i<10;i++) {
    uint8_t p[4]={0,0,0,0};
    int err=readRegBounded(0x70,p,4);
    if (err!=0) {
      Serial.print("REG70_FAIL error=");Serial.print(err);
      Serial.print(" events=0x");Serial.print(lastEventBits,HEX);
      Serial.print(" errorsrc=0x");Serial.println(lastErrsrc,HEX);
      validVersion=false;
      return;
    }
    if (p[0]==0) {evtEmpty++;break;}
    evtCount++;
    Serial.print("EVENT70 #");Serial.print(evtCount);
    Serial.print(" type=");Serial.print(p[0]);
    Serial.print(" A=0x");printHex(p[1]);
    Serial.print(" B=0x");printHex(p[2]);
    Serial.print(" C=0x");printHex(p[3]);
    if (p[0]==1) {
      Serial.print(" HID_KEY=0x");printHex(p[2]);
      Serial.print(" STATE=");Serial.print(p[3]);
    }
    Serial.println();
  }
}

void setup() {
  Serial.begin(115200);
  uint32_t t0=millis();
  while(!Serial && (uint32_t)(millis()-t0) < 3500UL) delay(10);
  Serial.println();
  Serial.println("BBQ20KBD I2C_DIAG_V2 NATIVE_TWIM_BOUNDED");
  Serial.println("GPIO INT=P1.13(D8) SDA=P0.20(D3) SCL=P0.09(D10), ADDR=0x1F");
  NRF_P1->PIN_CNF[INT_GPIO] =
    (GPIO_PIN_CNF_DIR_Input << GPIO_PIN_CNF_DIR_Pos) |
    (GPIO_PIN_CNF_INPUT_Connect << GPIO_PIN_CNF_INPUT_Pos) |
    (GPIO_PIN_CNF_PULL_Pullup << GPIO_PIN_CNF_PULL_Pos);
  releaseTWIM();
  Serial.println("BBQ20KBD I2C_DIAG_V3 PASSIVE_GPIO_FIRST");
  Serial.println("TWIM DISABLED, SDA/SCL INPUT PULLUP, NO I2C TRANSFERS");
  printBus("PASSIVE_START");
  Serial.println("COMMANDS: a=attach TWIM and probe, d=disable TWIM and release I2C lines");
  Serial.println("READY_V3");
  Serial.flush();
}

void loop() {
  while (Serial.available()) {
    char cmd=(char)Serial.read();
    if(cmd=='a'||cmd=='A') {
      startTWIM();
      Serial.println("TWIM_ATTACHED");
    } else if(cmd=='d'||cmd=='D') {
      releaseTWIM();
      Serial.println("TWIM_DISABLED");
    }
  }
  uint32_t now=millis();
  if ((uint32_t)(now-lastScan)>=SCAN_MS) {
    lastScan=now;
    if (twimActive) probe();
    else {
      Serial.print("PASSIVE ms="); Serial.print(now);
      printBus(" TWIM_DISABLED");
    }
  }
  if (twimActive && (uint32_t)(now-lastEvent)>=EVENT_MS) {
    lastEvent=now;
    pollEvents();
  }
  delay(1);
}
