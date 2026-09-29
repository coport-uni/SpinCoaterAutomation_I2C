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
  bus_check/                  Wire 없이 SDA·SCL·INT 레벨만 읽는 진단
  slave_selftest/             UNO Q 슬레이브 동작 자체 검증과 로그
  dual_target/                컨트롤러 하나에 타깃 2개 등록 검증
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

### ⚠️ 메인보드 버스는 5 V다 — 레벨 시프터 없이 연결하면 안 된다

2026-09-29 실측 결과 **메인보드 I2C 버스는 5 V**다. UNO Q 헤더의 절대 최대 전압은
3.6 V이므로 **직결하는 순간 보드가 파손된다.** BSS138 계열 레벨 시프터를 반드시
거쳐야 한다.

| 시프터 | 연결 |
| --- | --- |
| LV | UNO Q 3.3V |
| HV | 메인보드 5 V |
| GND | 양쪽 공통 |
| LV1 / HV1 | D20 SDA / X1 SDA |
| LV2 / HV2 | D21 SCL / X1 SCL |
| LV3 / HV3 | D2 INT / X1 INT |

INT도 SDA·SCL처럼 오픈드레인이라 양방향 채널에 그대로 맞는다. **메인보드 VDD는
UNO Q에 연결하지 않는다** — 아두이노는 USB로 전원을 받는다.

시프터 모듈은 양쪽 레일에 풀업을 달고 있는데, 이게 부수적으로 문제 하나를 해결한다.
키패드 보드는 SDA와 INT는 풀업하지만 **SCL은 풀업하지 않는다.** 원래 기계에서는
메인보드가 그 풀업을 대주고 있었다. UNO Q에서 이 문제가 안 보이는 이유는 Zephyr
pinctrl이 I2C 핀을 풀업으로 바이어스하기 때문이다. 다른 코어에서는 드러난다.

벤치 작업(키패드 단독)은 시프터가 필요 없다. 키패드를 UNO Q의 3.3 V로 직접 구동하면
되고, 1단계 전체를 그렇게 진행했다.

### 에뮬레이터 보드 선정

사양서는 3주소 동시 응답을 위해 Nano 3개 병렬 또는 AVR TWAMR 해킹을 제안했지만,
**Nano는 필요 없다.** UNO Q가 슬레이브로 동작한다는 것이 실측으로 확인됐다. 근거:

- `CONFIG_I2C_TARGET=y`로 슬레이브 모드가 빌드에 켜져 있다
- `Wire.begin(uint8_t address)`가 Zephyr `i2c_target_register()`를 호출하고
  `onReceive` / `onRequest` 콜백이 구현돼 있다
- I2C 컨트롤러가 3개 노출된다: `i2cs = <&i2c2>, <&i2c4>, <&i2c3>` → `Wire`, `Wire1`, `Wire2`
- `i2c3`은 PC0 = A5, PC1 = A4로 헤더에 나와 있다

컨트롤러마다 주소를 하나씩 맡기고 SDA/SCL을 병렬로 묶으면 `0x21`, `0x22`, `0x60`을
한 보드에서 낼 수 있다.

### 슬레이브 동작은 실측으로 확인됐다 (2026-09-29)

`claude_test/slave_selftest`가 점퍼 2개(A4↔D20, A5↔D21)만으로 이를 증명한다.
`Wire2`(i2c3)를 `0x21` 타깃으로 열고 `Wire`(i2c2)가 마스터로 접근한다.
키패드는 분리한 상태로 실행한다 — 키패드도 `0x21`이라 주소가 겹친다.

```text
ADDR 0x21
SCAN 1
WRITE ack=1
READ got=0x5A want=0x5A ok=1
CB req=1 recv=1 last=0xA5
RESULT PASS
```

주소 응답, 쓰기 ACK, 요청한 바이트 반환, `onReceive`/`onRequest` 콜백 호출까지
전부 동작한다. 로그는 `claude_test/slave_selftest/unoq_slave_verify.log`.

### 컨트롤러 배치

| 컨트롤러 | Arduino 핀 | STM32 핀 | 위치 |
| --- | --- | --- | --- |
| `Wire` = i2c2 | D20 SDA, D21 SCL | PB11, PB10 | 기본 헤더 |
| `Wire2` = i2c3 | A4 SDA, A5 SCL | PC1, PC0 | 기본 헤더 |
| `Wire1` = i2c4 | D42 SDA, D40 SCL | PF15, PF14 | 고밀도 커넥터 |

기본 헤더에는 컨트롤러가 2개뿐인데 에뮬레이터는 주소 3개가 필요하다. 하지만
**컨트롤러 하나가 주소 2개를 받을 수 있다는 것이 실측으로 확인됐다.** 따라서
고밀도 커넥터를 뜯을 필요도, `0x60`을 포기할 필요도 없다.

### 컨트롤러당 타깃 2개 (2026-09-29 실측)

STM32 I2C는 자체 주소 레지스터가 OA1·OA2 2개이고, Zephyr 드라이버도
`struct i2c_stm32_data`에 `target_cfg`와 `target2_cfg`를 갖는다. 두 번째는
`CONFIG_I2C_STM32_V2` 가드 안에 있는데, 이 빌드의 `autoconf.h`에서 `1`이다.

Arduino `Wire`는 인스턴스당 `i2c_target_config`가 하나라 두 번째 주소를 낼 수
없다. 대신 `Wire2.begin()`으로 컨트롤러를 올린 뒤 Zephyr의
`i2c_target_register()`를 같은 device에 **직접** 호출하면 된다. 스케치에서
`<zephyr/drivers/i2c.h>`와 `DEVICE_DT_GET(DT_NODELABEL(i2c3))`가 그대로 쓰인다.

`claude_test/dual_target`의 결과:

```text
REGISTER2 rc=0
ADDR 0x21
ADDR 0x22
SCAN 2
READ1 got=0x5A want=0x5A ok=1
READ2 got=0xB7 want=0xB7 ok=1
CB1 req=1 recv=1
CB2 req=1 recv=1 last=0xA5
RESULT PASS
```

두 주소가 각자의 바이트를 내주고 콜백도 독립적으로 호출된다. 로그는
`claude_test/dual_target/unoq_dual_target_verify.log`.

**컨트롤러 2개 × 주소 2개 = 4 ≥ 3.** 예를 들어 `Wire`에 `0x21`·`0x22`를,
`Wire2`에 `0x60`을 걸고 두 컨트롤러의 SDA/SCL을 묶어 시프터로 내보내면 된다.

> 아직 확인 안 된 것: 위 테스트는 `Wire`가 **마스터**인 구성이었다. 실제
> 에뮬레이터에서는 두 컨트롤러가 **모두 타깃**이고 메인보드가 마스터다. 동작에
> 문제는 없어 보이지만 이 구성 자체는 아직 실측하지 않았다.

### 미결 사항

| 항목 | 영향 |
| --- | --- |
| 메인보드 버스 전압 | **5 V 실측 완료.** 레벨 시프터 필수 |
| UNO Q 3주소 동시 응답 | **해소.** 슬레이브 동작과 컨트롤러당 타깃 2개 모두 실측 확인. 헤더의 컨트롤러 2개로 주소 4개까지 가능 |
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
