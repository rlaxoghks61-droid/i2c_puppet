# BBQ20KBD USB-C 없이 SWD로 펌웨어 기록하기 — 실험용

## 목적
Seeed XIAO nRF52840을 USB HID CMSIS-DAP v1 프로그래머로 사용.
RP2040 기반 BBQ20KBD 원래 USB-C 포트가 없어도, RP2040의 SWD 핀으로
GitHub Actions에서 빌드한 \`i2c_puppet.elf\`를 OpenOCD로 기록한다.

## 배선
| XIAO nRF52840 | BBQ20KBD RP2040 |
| --- | --- |
| D8 (P1.13) | SWCLK (RP2040 SWCLK 전용 패드) |
| D9 (P1.14) | SWDIO (RP2040 SWDIO 전용 패드) |
| GND | GND (기존 공통 접지 유지) |

**D8/D9은 물리적인 XIAO 출력 핀, SWCLK/SWDIO는 RP2040의 별도 SWD 패드임.**
기존 I2C SDA/SCL과는 다르다. RP2040은 정상 3.3 V 전원이 공급되어야 한다.
GPIO에 USB 5V를 절대로 연결하지 않는다.

## 업로드
1. Arduino IDE에서 Seeed nRF52 Boards 설치, 보드 = Seeed XIAO nRF52840.
2. 이 폴더의 \`NRF52_XIAO_SWD.ino\`를 Arduino IDE에서 연다.
3. \`dap.c\`, \`dap.h\`, \`dap_config.h\`는 같은 스케치 폴더에 두어야 한다.
4. 업로드하면 XIAO는 기존 키보드/BLE 펌웨어 대신 USB CMSIS-DAP으로 작동한다.
5. Windows 장치 관리자 / OpenOCD에서 CMSIS-DAP HID를 확인한다.

## PC에서 RP2040 쓰기
\`\`\`
openocd -f interface/cmsis-dap.cfg -f target/rp2040.cfg -c "adapter speed 100" -c "program i2c_puppet.elf verify reset exit"
\`\`\`
\`i2c_puppet.elf\`는 GitHub Actions 성공 빌드 Artifacts ZIP에 들어 있다.
OpenOCD는 Raspberry Pi의 RP2040 지원 버전을 권장한다.

## 검증
이 펌웨어는 Free-DAP 코어를 nRF52840 Arduino/TinyUSB로 이식한 **실험판**이다.
실제 XIAO+RP2040 실기로 OpenOCD 연결·플래시를 성공시킨 검증은 아직 없다.
GitHub Actions에서 Arduino 컴파일 결과를 검토하고 보드에 설치한다.
USB HID에 문제가 있으면 nRF52840 Reset 두 번으로 부트로더 진입 후
이전 정상 INO를 업로드한다.

Upstream: https://github.com/ataradov/free-dap
Free-DAP LICENSE is included verbatim, BSD-3-Clause.
