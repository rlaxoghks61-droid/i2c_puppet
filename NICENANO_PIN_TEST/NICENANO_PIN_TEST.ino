// nice!nano v2 nRF52840 GPIO mapping tester. USB CDC Serial protocol.
// X: user-confirmed XIAO-shaped D labels. N: official nice!nano Arduino core.
// Active pin is output; all other scanned GPIO pins are left as inputs.
#include <Arduino.h>
#include <Adafruit_TinyUSB.h>
#include <nrf.h>
#include <stdio.h>

struct PinAddr { uint8_t port; uint8_t pin; };
static const PinAddr map_xiao[11] = {
  {0,2},{0,3},{0,28},{0,29},{0,4},{0,5},
  {1,11},{1,12},{1,13},{1,14},{1,15}
};
static const PinAddr map_nice[11] = {
  {0,6},{0,8},{0,17},{0,20},{0,22},{0,24},
  {1,0},{0,11},{1,4},{1,6},{0,9}
};

static bool active = false;
static PinAddr active_pin = {0,0};
static uint32_t activated_ms = 0;
static const uint32_t AUTO_RELEASE_MS = 120000UL;

static NRF_GPIO_Type *port_regs(PinAddr p) {
  return p.port ? NRF_P1 : NRF_P0;
}
static uint32_t mask(PinAddr p) { return 1UL << p.pin; }

static void release_pin() {
  if (!active) return;
  NRF_GPIO_Type *r = port_regs(active_pin);
  r->DIRCLR = mask(active_pin);
  r->PIN_CNF[active_pin.pin] = (GPIO_PIN_CNF_INPUT_Connect << GPIO_PIN_CNF_INPUT_Pos);
  active = false;
}

static bool nfc_reserved(PinAddr p) {
  // Respect existing UICR. NEVER write UICR to reconfigure NFC pads.
  return p.port == 0 && (p.pin == 9 || p.pin == 10) && (NRF_UICR->NFCPINS & 1UL);
}

static void reply_state(const char *profile, uint8_t d, PinAddr p, char cmd, bool state) {
  Serial.print("OK PROFILE="); Serial.print(profile);
  Serial.print(" D"); Serial.print(d);
  Serial.print(" GPIO=P"); Serial.print(p.port); Serial.print('.');
  if (p.pin < 10) Serial.print('0');
  Serial.print(p.pin);
  Serial.print(" CMD="); Serial.print(cmd == 'H' ? "HIGH" : "LOW");
  Serial.print(" READ="); Serial.println(state ? "HIGH" : "LOW");
}

void setup() {
  Serial.begin(115200);
  Serial.setTimeout(80);
}

void loop() {
  if (active && (uint32_t)(millis() - activated_ms) >= AUTO_RELEASE_MS) {
    release_pin();
    if (Serial) Serial.println("TIMEOUT OUTPUT RELEASED");
  }
  if (!Serial || !Serial.available()) return;
  String cmd = Serial.readStringUntil('\n');
  cmd.trim();
  cmd.toUpperCase();

  if (cmd == "HELLO") {
    Serial.println("PINSCAN V1; X=XIAO-style, N=nice!nano-core; NFC/UICR unchanged; 120s auto-release");
    return;
  }
  if (cmd == "OFF") {
    release_pin();
    Serial.println("OFF OK ALL TEST OUTPUTS RELEASED");
    return;
  }

  char profile = 0, level = 0;
  int num = -1;
  if (sscanf(cmd.c_str(), "SET %c %d %c", &profile, &num, &level) != 3 ||
      (profile != 'X' && profile != 'N') || (level != 'H' && level != 'L') ||
      num < 0 || num > 10) {
    Serial.println("ERR COMMAND: HELLO | SET X|N index H|L | OFF");
    return;
  }

  release_pin();
  PinAddr p = profile == 'X' ? map_xiao[num] : map_nice[num];
  if (nfc_reserved(p)) {
    Serial.println("SKIP NFC_PIN_RESERVED UICR_UNCHANGED");
    return;
  }
  NRF_GPIO_Type *r = port_regs(p);
  uint32_t bit = mask(p);
  r->DIRCLR = bit;
  r->PIN_CNF[p.pin] = (GPIO_PIN_CNF_INPUT_Connect << GPIO_PIN_CNF_INPUT_Pos);
  if (level == 'H') r->OUTSET = bit;
  else r->OUTCLR = bit;
  r->DIRSET = bit;

  active_pin = p;
  active = true;
  activated_ms = millis();
  delay(2);
  reply_state(profile == 'X' ? "XIAO" : "NICE", (uint8_t)num, p, level,
              (r->IN & bit) != 0);
}
