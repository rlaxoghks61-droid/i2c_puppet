/*
  nice!nano v2 -> RP2040 (BBQ20KBD) firmware 1.1 I2C verification.
  IMPORTANT: This firmware is a temporary I2C/keyboard-event diagnostic.
  Proven wiring: D3 INT=P0.29, D4 SDA=P0.04, D5 SCL=P0.05.
  Standard i2c_puppet on RP2040: 7-bit address 0x1F;
    REG_VER=0x01 (1 byte), REG_KEY=0x04 (1 byte FIFO count),
    REG_FIF=0x09 (2 bytes: state, ASCII key). NOT 0x70/4 bytes.
  No BLE or SWD on this diagnostic image.
  USB Serial 115200; prints STATUS and KEY lines.
*/
#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_TinyUSB.h>
#include <nrf.h>

static const uint8_t I2C_ADDR = 0x1F;
static const uint8_t SDA_GPIO = 4;
static const uint8_t SCL_GPIO = 5;
static const uint8_t INT_GPIO = 29;
static const uint32_t STATUS_INTERVAL = 1000;
static uint32_t lastStatus = 0, lastPoll = 0;
static uint32_t addrOk = 0, addrFail = 0, verOk = 0, verFail = 0, keyOk = 0, keyFail = 0, fifoOk = 0, fifoFail = 0;
static uint8_t lastVer = 0, lastCount = 0;
static int lastErr = 0;

static void i2cPin(uint8_t pin) {
  NRF_P0->PIN_CNF[pin] =
    (GPIO_PIN_CNF_DIR_Input << GPIO_PIN_CNF_DIR_Pos) |
    (GPIO_PIN_CNF_INPUT_Connect << GPIO_PIN_CNF_INPUT_Pos) |
    (GPIO_PIN_CNF_PULL_Pullup << GPIO_PIN_CNF_PULL_Pos) |
    (GPIO_PIN_CNF_DRIVE_S0D1 << GPIO_PIN_CNF_DRIVE_Pos) |
    (GPIO_PIN_CNF_SENSE_Disabled << GPIO_PIN_CNF_SENSE_Pos);
}

static void setupI2C() {
  Wire.begin();
  Wire.setClock(100000);
  NRF_TWIM0->ENABLE = (TWIM_ENABLE_ENABLE_Disabled << TWIM_ENABLE_ENABLE_Pos);
  i2cPin(SDA_GPIO);
  i2cPin(SCL_GPIO);
  NRF_TWIM0->PSEL.SDA = SDA_GPIO;
  NRF_TWIM0->PSEL.SCL = SCL_GPIO;
  NRF_TWIM0->ENABLE = (TWIM_ENABLE_ENABLE_Enabled << TWIM_ENABLE_ENABLE_Pos);
  NRF_P0->PIN_CNF[INT_GPIO] =
    (GPIO_PIN_CNF_DIR_Input << GPIO_PIN_CNF_DIR_Pos) |
    (GPIO_PIN_CNF_INPUT_Connect << GPIO_PIN_CNF_INPUT_Pos) |
    (GPIO_PIN_CNF_PULL_Pullup << GPIO_PIN_CNF_PULL_Pos);
}

static int irq() { return (NRF_P0->IN & (1UL << INT_GPIO)) ? 1 : 0; }
static int sda() { return (NRF_P0->IN & (1UL << SDA_GPIO)) ? 1 : 0; }
static int scl() { return (NRF_P0->IN & (1UL << SCL_GPIO)) ? 1 : 0; }

static bool regRead(uint8_t reg, uint8_t *out, uint8_t n, int *err) {
  Wire.beginTransmission(I2C_ADDR);
  Wire.write(reg);
  *err = Wire.endTransmission(false);
  if (*err != 0) return false;
  const uint8_t received = Wire.requestFrom((uint8_t)I2C_ADDR, n, (uint8_t)true);
  if (received != n) {
    *err = -1 - (int)received;
    while (Wire.available()) Wire.read();
    return false;
  }
  for (uint8_t i = 0; i < n; i++) out[i] = Wire.read();
  *err = 0;
  return true;
}

static void printByteHex(uint8_t x) {
  if (x < 16) Serial.print('0');
  Serial.print(x, HEX);
}
static void queryStatus() {
  Wire.beginTransmission(I2C_ADDR);
  lastErr = Wire.endTransmission(true);
  if (lastErr == 0) addrOk++; else addrFail++;

  uint8_t version = 0;
  int regErr = 0;
  if (regRead(0x01, &version, 1, &regErr)) { verOk++; lastVer=version; }
  else { verFail++; if (lastErr == 0) lastErr=regErr; }

  Serial.print("STATUS ACK="); Serial.print(lastErr == 0 ? 1 : 0); // will be updated by register error too
  Serial.print(" ack_err="); Serial.print(lastErr);
  Serial.print(" addr_ok="); Serial.print(addrOk);
  Serial.print(" addr_fail="); Serial.print(addrFail);
  Serial.print(" ver_ok="); Serial.print(verOk);
  Serial.print(" ver_fail="); Serial.print(verFail);
  Serial.print(" FW=0x"); printByteHex(lastVer);
  Serial.print(" fifo_count="); Serial.print(lastCount);
  Serial.print(" INT="); Serial.print(irq());
  Serial.print(" SDA="); Serial.print(sda());
  Serial.print(" SCL="); Serial.println(scl());
}

static void pollKeyboard() {
  uint8_t countReg = 0;
  int err = 0;
  if (!regRead(0x04, &countReg, 1, &err)) {
    keyFail++;
    static uint32_t lastE = 0;
    if (millis() - lastE > 2000) {
      Serial.print("READ_KEY_FAIL err="); Serial.println(err);
      lastE = millis();
    }
    return;
  }
  keyOk++;
  uint8_t count = countReg & 0x1f;
  lastCount=count;
  if (count == 0) return;
  if (count > 12) count = 12;
  for (uint8_t i = 0; i < count; i++) {
    uint8_t event[2] = {0,0};
    if (!regRead(0x09, event, 2, &err)) {
      fifoFail++;
      Serial.print("FIFO_READ_FAIL err="); Serial.println(err);
      return;
    }
    fifoOk++;
    Serial.print("KEY STATE="); Serial.print(event[0]);
    Serial.print(" CODE=0x"); printByteHex(event[1]);
    Serial.print(" CHAR=");
    if (event[1] >= 32 && event[1] < 127) Serial.write(event[1]);
    else Serial.print("(nonprintable)");
    Serial.println();
  }
}

void setup() {
  Serial.begin(115200);
  unsigned long start = millis();
  while (!Serial && millis() - start < 3500) delay(10);
  Serial.println("\nBBQ20KBD I2C_PUPPET 0x1F v1.1 DIAGNOSTIC");
  Serial.println("INT=D3(P0.29) SDA=D4(P0.04) SCL=D5(P0.05)");
  Serial.println("REG_VER=01 REG_KEY=04 REG_FIFO=09(2 bytes)");
  setupI2C();
  Serial.print("TWIM0 PSEL SDA="); Serial.print(NRF_TWIM0->PSEL.SDA);
  Serial.print(" SCL="); Serial.println(NRF_TWIM0->PSEL.SCL);
  Serial.println("READY");
}

void loop() {
  const uint32_t now = millis();
  if (now - lastPoll >= 30) {
    lastPoll=now;
    pollKeyboard();
  }
  if (now - lastStatus >= STATUS_INTERVAL) {
    lastStatus=now;
    queryStatus();
  }
  delay(1);
}
