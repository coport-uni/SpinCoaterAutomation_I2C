# SpinCoaterAutomation_I2C

Laurell 스핀코터의 키패드 보드(Ellenby 09-0128-00)를 I2C로 리버스 엔지니어링해서,
최종적으로 키패드를 대신하는 PCA9555 슬레이브 에뮬레이터로 스핀코터를 자동 제어하는
프로젝트다. 개발 사양서는 [docs/spincoater_keypad_spec.md](docs/spincoater_keypad_spec.md)에 있다.

## 현재 상태

사양서가 정의한 3단계 중 **1단계 탐색이 완료**됐다.

| 단계 | 목표 | 상태 |
| --- | --- | --- |
| 1. 탐색 | 주소, 버튼 비트맵, LED 맵 확보 | **완료** (2026-09-29) |
| 2. 기록 | 메인보드 트랜잭션 로그 확보 | 착수 전 |
| 3. 에뮬레이션 | 키 입력 주입으로 스핀코터 제어 | 착수 전 |

키패드 보드를 메인보드에서 분리해 Arduino UNO Q의 3.3 V로 단독 구동한 상태에서,
18개 버튼과 16개 LED를 전부 실기로 매핑했다. 아직 메인보드에는 연결한 적이 없다.

## 하드웨어 구성

키패드 보드는 메인보드와 5핀 헤더 X1 하나로만 연결된다. LCD는 별도 FPC로 메인보드에
직결되어 이 저장소의 범위 밖이다.

| 위치 | 칩 | 역할 | I2C 주소 |
| --- | --- | --- | --- |
| U4 | PCA9555D | 버튼 16개 | `0x21` |
| U6 | PCA9555D | 버튼 2개 + LED 2개(추정) | `0x22` |
| U5 | PCA9532D | LED 16개 | `0x60` |
| U1~U3 | MAX6818EAP | 스위치 디바운서, 약 40 ms | 없음 |

### 헤더 X1 핀맵

| 핀 | 신호 | 근거 |
| --- | --- | --- |
| 1 | VDD | 도통 확인 |
| 2 | GND | 도통 확인 |
| 3 | SDA | 도통 확인 |
| 4 | SCL | R16 1 kΩ 직렬. 100 kHz 버스 정상 동작으로 실증 |
| 5 | **INT** | 실측 확정. RESET 아님 |

### 1단계 배선 (키패드 단독, 메인보드 미연결)

| UNO Q | 키패드 X1 |
| --- | --- |
| 3.3V | VDD |
| GND | GND |
| D20 (SDA) | SDA |
| D21 (SCL) | SCL |
| D2 | INT |

> UNO Q 헤더의 절대 최대 전압은 3.6 V다. 5V 핀은 절대 쓰지 않는다.

## 확인된 사실

### 버튼 맵

전체는 [docs/button_map.json](docs/button_map.json)에 있다. 눌린 키는 해당 비트를 `0`으로
떨어뜨리고, **누르고 있는 동안 계속 0을 유지**한다(레벨 방식).

**`0x21` 입력 포트 0 (`0x00`)**

| 비트 | 7 | 6 | 5 | 4 | 3 | 2 | 1 | 0 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 버튼 | SELECT PROCESS | VACUUM | F1 | F2 | → | FWD | pg dn | pg up |

**`0x21` 입력 포트 1 (`0x01`)**

| 비트 | 7 | 6 | 5 | 4 | 3 | 2 | 1 | 0 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 버튼 | ↑ | INFO | START | STOP | PAUSE | ← | REV | ↓ |

**`0x22` 입력 포트 0 (`0x00`)** — 비트 1 = RUN MODE, 비트 0 = EDIT MODE.
나머지 14비트는 버튼 조작에 반응하지 않았고 용도 미상이다.

### LED 맵

전체는 [docs/led_map.json](docs/led_map.json)에 있다.

**PCA9532 채널 n = `0x21` 비트 n의 버튼 LED.** 입력 포트 0의 비트 0~7이 채널 0~7,
입력 포트 1의 비트 0~7이 채널 8~15로 그대로 이어진다. 버튼과 LED가 같은 인덱스를 쓴다.

LED는 18개인데 PCA9532는 16채널이다. 헤더 X1에 VDD·GND·SDA·SCL·INT 5개뿐이라 모든
LED는 I2C로만 제어될 수 있고, `0x21`의 16핀은 전부 버튼이므로 **EDIT MODE와 RUN MODE의
LED는 `0x22`에 있을 수밖에 없다.** 정확한 핀은 2단계에서 메인보드의 쓰기를 관찰해 확정한다.

### LED가 상태 피드백 채널인 점

이 스핀코터는 **현재 상태에서 누를 수 있는 버튼만 LED를 켠다.** 따라서 메인보드가
`0x60`에 쓰는 LS 레지스터 값은 곧 "지금 유효한 명령 목록"이다. 에뮬레이터가 이 쓰기를
로깅하면 호스트는 다음을 할 수 있다.

- 주입 전에 그 키가 지금 유효한지 확인 → 무시될 명령을 보내지 않음
- 주입 후 LED 집합 변화로 키가 먹었는지 확인
- 고정 딜레이 대신 상태 기반 대기

`host/keypad_client.py`는 이 점을 전제로 설계해야 한다. 블라인드 `press START`가 아니라
`wait_until_valid(START)` 후 `press START`다.

### INT 동작

PCA9555 데이터시트 그대로다. 입력 변화 시 LOW, 입력 레지스터가 읽히면 HIGH로 자동 복귀.
키를 계속 누르고 있어도 INT는 HIGH를 유지하고 다음 변화에서만 다시 LOW로 간다.

### 하드웨어 이상

- **VACUUM 돔 스위치 접촉 불량**: 의도적으로 3번 눌렀을 때 1번만 검출됐고, 2초 누름에
  488 ms만 유지됐다. 1차 매핑에서는 아예 검출되지 않았다. 에뮬레이션에는 영향이 없다
  (에뮬레이터가 물리 스위치를 거치지 않음)
- **→ 키 바운스**: MAX6818 디바운서에도 불구하고 123 ms 바운스가 1회 관측됐다

## 개발 환경

### 필요한 것

| 도구 | 비고 |
| --- | --- |
| Arduino IDE | 번들된 `arduino-cli`를 사용한다 |
| `arduino:zephyr` 코어 1.0.0 | `adb` 32.0.0이 함께 설치된다. 별도 platform-tools 불필요 |
| `Arduino_RouterBridge` 라이브러리 | 없으면 컴파일이 `#error`로 실패한다 |
| `gh` CLI | 이슈 관리 |

```sh
CLI="/c/Program Files/Arduino IDE/resources/app/lib/backend/resources/arduino-cli.exe"
"$CLI" core install arduino:zephyr@1.0.0
"$CLI" lib install Arduino_RouterBridge
```

### UNO Q의 시리얼 구조 (중요)

UNO Q에서 MCU의 `Serial`은 호스트 PC로 나오지 않는다. 구조가 이렇다.

```
스케치의 Monitor 객체
  └─> Router Bridge (UART /dev/ttyHS1)
        └─> arduino-router (Linux 측, 127.0.0.1:7500)
              └─> socat (arduino-router-serial.service)
                    └─> /dev/ttyGS0  ─── USB ───  호스트 COM 포트
```

따라서 **스케치는 `Serial`이 아니라 `Monitor`로 출력해야 한다.**

```c
#include <Arduino_RouterBridge.h>

void setup() {
    Serial.begin(115200);
    Bridge.begin();
    Monitor.begin(115200);
    while (!Monitor) { delay(500); }
    Monitor.println("hello");   // Serial.println이 아니다
}
```

그리고 호스트의 `arduino-cli monitor -p COM17`로는 아무것도 읽히지 않는 경우가 있다.
확실한 방법은 adb로 Linux 측 소켓을 직접 읽는 것이다.

```sh
ADB="$LOCALAPPDATA/Arduino15/packages/arduino/tools/adb/32.0.0/adb.exe"
"$ADB" shell "nc 127.0.0.1 7500"
```

`setup()` 출력은 부팅 직후 한 번만 나온다. 캡처를 먼저 걸고 재업로드해서 리셋시켜야
초기 레지스터 덤프를 놓치지 않는다.

### 빌드와 업로드

```sh
"$CLI" compile --fqbn arduino:zephyr:unoq firmware/i2c_scan
"$CLI" upload  --fqbn arduino:zephyr:unoq -p COM17 firmware/i2c_scan
```

포트 번호는 환경마다 다르다. `"$CLI" board list`로 확인한다.
업로드는 Linux 측이 SWD로 STM32U585에 직접 쓰는 방식이라 부트로더 버튼 조작이 필요 없다.

## 저장소 구조

```text
docs/
  spincoater_keypad_spec.md   개발 사양서
  ButtonLayout.jpg            키패드 전면 레이아웃
  button_map.json             버튼 18개 매핑
  led_map.json                LED 16개 매핑 + 나머지 2개 추론
firmware/
  i2c_scan/                   버스 주소 스캔
  pca9532_led/                LED 제어, 매핑용 순회 기능 포함
claude_test/
  keypad_probe/               버튼 매핑 프로브와 실측 로그
```

### 펌웨어

| 스케치 | 역할 | 상태 |
| --- | --- | --- |
| `i2c_scan` | 버스 주소 스캔 | 실기 확인 완료 |
| `pca9532_led` | LED 제어 (`LED n on/off/pwm0/pwm1`, `ALL off`, `WALK ms`, `HALT`) | 실기 확인 완료 |
| `pca9555_poll` | 버튼 폴링 | 미작성. `claude_test/keypad_probe`가 역할을 대신하는 중 |
| `pca9555_emu` | 슬레이브 에뮬레이터 | 미작성 |

`pca9532_led`는 `ALL on`을 의도적으로 거부한다. 3.3 V 벤치 구동 중에 16개를 동시
점등시키지 않기 위해서다.

## 다음 단계

### ⚠️ 메인보드 연결 전 필수 확인

**메인보드 헤더의 VDD 전압을 반드시 먼저 측정해야 한다.** UNO Q 헤더의 절대 최대
전압은 3.6 V다. 이 보드는 2005년 설계이고 PCA9555·PCA9532·MAX6818 모두 5 V 동작
범위라 버스가 5 V일 가능성이 있다. 5 V면 BSS138 레벨 시프터 없이 연결하는 순간
UNO Q가 파손된다. 이 순서는 바꾸면 안 된다.

### 에뮬레이터 보드 선정

사양서는 3주소 동시 응답을 위해 Nano 3개 병렬 또는 AVR TWAMR 해킹을 제안했지만,
UNO Q 한 장으로 가능할 수 있다는 정황이 있다. 코어 소스 확인 결과:

- `CONFIG_I2C_TARGET=y`로 슬레이브 모드가 빌드에 켜져 있다
- `Wire.begin(uint8_t address)`가 Zephyr `i2c_target_register()`를 호출하고
  `onReceive` / `onRequest` 콜백이 구현돼 있다
- I2C 컨트롤러가 3개 노출된다: `i2cs = <&i2c2>, <&i2c4>, <&i2c3>` → `Wire`, `Wire1`, `Wire2`
- `i2c3`은 PC0 = A5, PC1 = A4로 헤더에 나와 있다

컨트롤러마다 주소를 하나씩 맡기고 SDA/SCL을 병렬로 묶으면 `0x21`, `0x22`, `0x60`을
한 보드에서 낼 수 있을지 모른다. **단 이는 소스 판독이지 실측이 아니다.**

추가 하드웨어 없이 검증하는 방법: **A4 → D20, A5 → D21** 점퍼 2개를 연결하면 같은
UNO Q 안에서 `Wire`가 마스터, `Wire2`가 슬레이브가 된다. 이때 키패드 보드는 분리해야
한다(`0x21` 주소 충돌).

### 미결 사항

| 항목 | 영향 |
| --- | --- |
| 메인보드 버스 전압 | 5 V면 UNO Q 직결 불가 |
| UNO Q 3주소 동시 응답 | 안 되면 Nano 3개로 전환 |
| 메인보드 폴링 순서·주기·초기화 시퀀스 | 2단계의 목표 그 자체 |
| 키 인식 최소 유지 시간 | 100 ms부터 50 ms 단위로 탐색 |
| `0x22`의 LED 2개 핀 위치 | 2단계 로그로 확정 |
| X1에 꽂을 물리 커넥터 | 미준비 |

버튼 맵과 LED 맵은 각 1회 확인 상태다. 사양서 §7은 2회 이상 재현을 요구하므로
검증 패스가 남아 있다.

## 참고 자료

- [NXP PCA9555 데이터시트](https://www.nxp.com/docs/en/data-sheet/PCA9555.pdf)
- [NXP PCA9532 데이터시트](https://www.nxp.com/docs/en/data-sheet/PCA9532.pdf)
- [Arduino UNO Q Power Specifications](https://docs.arduino.cc/tutorials/uno-q/power-specification/)
- [coport-uni/CommonClaude](https://github.com/coport-uni/CommonClaude)
