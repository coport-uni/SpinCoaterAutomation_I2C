# 스핀코터 키패드 I2C 리버스 엔지니어링 개발 사양서

작성일: 2026-09-29

## 1. 목표와 범위

스핀코터 키패드 보드 09-0128-00의 I2C 인터페이스를 Arduino UNO Q로 분석하고, 최종적으로 메인보드에 키 입력을 주입하는 PCA9555 슬레이브 에뮬레이터를 만든다. 개발은 UNO Q가 연결된 호스트 PC에서 CommonClaude 하네스 위의 Claude Code로 진행한다.

| 단계 | 목표 | 완료 기준 |
| --- | --- | --- |
| 1. 탐색 | 키패드 보드 주소, 버튼 비트맵, LED 맵 확보 | 18 버튼과 18 LED 매핑표 100% 작성 |
| 2. 기록 | 메인보드가 키패드에 보내는 I2C 트랜잭션 기록 | 폴링 주기 ms, 레지스터 순서, LED 명령 로그 확보 |
| 3. 에뮬레이션 | MCU가 키패드 보드를 대신해 메인보드에 키 입력 주입 | PC 명령으로 스핀코터 Start와 Stop 동작 성공 |

범위 밖: LCD 데이터 경로, 모터 드라이버, 메인보드 펌웨어 수정.

## 2. 하드웨어 현황

키패드 보드는 I2C 슬레이브 3개와 디바운서 3개로 구성되며, 메인보드와는 5핀 헤더 X1 하나로만 연결된다. LCD는 별도 FPC로 메인보드에 직결되어 이 사양서의 범위 밖이다.

| 위치 | 칩 | 역할 | 주소 범위 | 동작 전압 |
| --- | --- | --- | --- | --- |
| U4, U6 | [PCA9555D](https://www.nxp.com/docs/en/data-sheet/PCA9555.pdf) | 16bit I2C GPIO expander, 버튼 입력 | 0x20 ~ 0x27 | 2.3 ~ 5.5 V |
| U5 | [PCA9532D](https://www.nxp.com/docs/en/data-sheet/PCA9532.pdf) | 16bit I2C LED dimmer, PWM 2채널 | 0x60 ~ 0x67 | 2.3 ~ 5.5 V |
| U1, U2, U3 | MAX6818EAP | 8채널 switch debouncer, 약 40 ms | 없음 | 2.7 ~ 5.5 V |

### 측정 대상 핀

| 신호 | PCA9555D | PCA9532D | MAX6818EAP |
| --- | --- | --- | --- |
| VDD | 24 | 24 | 20 |
| VSS, GND | 12 | 12 | 9 |
| SDA | 23 | 23 | 없음 |
| SCL | 22 | 22 | 없음 |
| INT, CH | 1 | 없음 | 11 |
| RESET | 없음 | 21 | 없음 |
| 주소 핀 | 21 A0, 2 A1, 3 A2 | 1 A0, 2 A1, 3 A2 | 없음 |

핀 1번은 몸체의 점 또는 경사면 쪽이며, 점을 왼쪽 위에 두었을 때 왼쪽 열은 위에서 아래로, 오른쪽 열은 아래에서 위로 번호가 올라간다. PCA9532D는 점이 없으므로 C3 커패시터 양단과 네 모서리 핀의 도통으로 24번 VDD와 12번 VSS를 먼저 잡는다.

### 도통 테스트로 확인된 사실, 2026-09-29

- 헤더 X1의 VDD, GND, SDA는 칩 핀과 직접 도통.
- SCL은 부저 도통 불가. 헤더 옆 R16 1 kΩ이 직렬로 들어간 것으로 추정, 저항 모드 측정으로 확정 필요.
- 보드 실크: 09-0128-00 Rev, Copyright 2005 Ellenby Technologies. 공개 회로도 없음.

### 헤더 X1 추정 핀맵

| 핀 | 신호 | 상태 |
| --- | --- | --- |
| 1 | VDD | 도통 확인 |
| 2 | GND | 도통 확인 |
| 3 | SDA | 도통 확인 |
| 4 | SCL | R16 경유 추정 |
| 5 | INT 또는 RESET | 미확인 |

## 3. UNO Q와 디바이스가 해야 하는 것

UNO Q는 3.3 V 로직이므로 키패드 보드를 UNO Q의 3.3 V로 구동해 레벨 시프터 없이 탐색한다. 메인보드 연결 단계에서만 5 V 대응이 필요하다.

### UNO Q 전기 조건

| 항목 | 값 | 출처 |
| --- | --- | --- |
| MCU 로직 전압 | 3.3 V | [UNO Q Power Specifications](https://docs.arduino.cc/tutorials/uno-q/power-specification/) |
| 헤더 절대 최대 전압 | 3.6 V | 같은 문서 |
| 기본 I2C 핀 | D20 SDA, D21 SCL, AREF 옆 | [UNO Q Getting Started](https://www.visuino.com/arduino-uno-q-getting-started/) |
| 스케치 실행 환경 | STM32U585 위 Zephyr 기반 Arduino Core | 같은 문서 |

### 1단계 배선, 키패드 보드 단독

| UNO Q | 키패드 X1 | 비고 |
| --- | --- | --- |
| 3.3V | VDD | 5V 핀 사용 금지 |
| GND | GND | GND 먼저 연결 |
| D20 SDA | SDA | |
| D21 SCL | SCL | R16 1 kΩ 직렬은 그대로 둠 |
| D2 | INT | 선택 |

풀업은 보드 SDA와 VDD 사이 저항이 무한대일 때만 4.7 kΩ을 3.3 V로 추가한다.

### 단계별 작업

1. 전원 끈 상태에서 저항 모드로 헤더 4번과 PCA9555 22번 사이 값을 재 R16 직렬 여부를 확정한다. 900 Ω에서 1,100 Ω이면 직렬.
2. 주소 핀 A0, A1, A2를 VDD 또는 GND와 도통시켜 U4, U6, U5의 주소를 계산한다. PCA9555는 0x20 + 4A2 + 2A1 + A0, PCA9532는 0x60 + 4A2 + 2A1 + A0.
3. UNO Q 3.3 V로 키패드 보드를 구동하고 I2C 스캐너로 주소 3개를 확인한다.
4. PCA9555 설정 레지스터 0x06, 0x07을 읽어 입력 핀과 출력 핀을 구분한다.
5. S1부터 S18까지 각각 2초 이상 누른 채 50 ms 폴링으로 주소와 비트를 기록한다. 입력 레지스터는 레벨 방식이라 누르고 있는 동안 0이 유지된다.
6. PCA9532 LS0부터 LS3에 값을 써 D1부터 D16을 매핑하고, 남은 LED 2개는 PCA9555 출력 비트에서 찾는다.
7. 2단계: 키패드 보드 대신 슬레이브 에뮬레이터를 메인보드에 꽂고 트랜잭션 로그를 확보한다. 메인보드 버스 전압이 5 V이면 BSS138 레벨 시프터를 넣는다.
8. 3단계: 로그로 확정한 폴링 순서에 맞춰 키 입력 주입 명령을 호스트 PC에서 시리얼로 보낸다.

### 단계 2와 3의 보드 선택

| 후보 | 장점 | 단점 |
| --- | --- | --- |
| UNO Q | 이미 보유, Linux 측에서 로그 처리 가능 | Zephyr 코어의 Wire 슬레이브 지원 미검증, 3.3 V |
| Nano 3개 | Wire 슬레이브 검증됨, 5 V 직결 | 주소당 1개 필요 |
| ESP32 | 슬레이브 지원, WiFi 로그 | 3.3 V, 단일 주소 |

1단계는 UNO Q로 시작하고, 2단계 전에 UNO Q 슬레이브 모드 가부를 별도 테스트로 확인한다.

## 4. 펌웨어 사양

펌웨어는 스케치 4개로 나누고, 모두 시리얼 115200 bps로 호스트 PC와 한 줄 텍스트 프로토콜로 통신한다. 매핑 결과는 펌웨어에 하드코딩하지 않고 호스트 측 JSON 파일에 둔다.

| 스케치 | 역할 | 입력 | 출력 |
| --- | --- | --- | --- |
| `i2c_scan` | 버스 주소 스캔 | 없음 | `ADDR 0x20`, `ADDR 0x21`, `ADDR 0x60` 형식 |
| `pca9555_poll` | 두 PCA9555의 0x00, 0x01을 50 ms 주기로 읽고 변화 비트만 출력 | 없음 | `CHG addr reg bit val ms` |
| `pca9532_led` | LS 레지스터에 값 쓰기 | `LED n on/off/pwm0/pwm1` | `OK` 또는 `ERR` |
| `pca9555_emu` | 슬레이브 에뮬레이터, 메인보드 트랜잭션 로그, 키 주입 | `PRESS n ms`, `HOLD n`, `RELEASE n` | `RX addr reg data ms` |

### 레지스터 참조

| 칩 | 레지스터 | 의미 |
| --- | --- | --- |
| PCA9555 | 0x00, 0x01 | 입력 포트 0, 1, 읽기 전용 |
| PCA9555 | 0x02, 0x03 | 출력 포트 0, 1 |
| PCA9555 | 0x06, 0x07 | 설정, 비트 1이 입력 |
| PCA9532 | 0x02, 0x03 | PSC0, PWM0 |
| PCA9532 | 0x06 ~ 0x09 | LS0 ~ LS3, LED당 2비트, 00 꺼짐 01 켜짐 10 PWM0 11 PWM1 |
| PCA9532 | 제어 바이트 bit 4 | Auto Increment |

### 에뮬레이터 요구사항

- 주소 3개 동시 응답. Nano 3개 병렬 또는 AVR TWAMR 0xFE 설정 후 자체 TWI ISR로 TWDR에서 주소 판별.
- 입력 레지스터 기본값 0xFF, 키 주입 시 해당 비트 0으로 유지. 유지 시간 기본 100 ms, 명령으로 조정.
- INT 핀은 입력 레지스터 변화 시 LOW로 떨어뜨리고 메인보드가 읽으면 HIGH로 복귀. PCA9555 데이터시트 동작과 동일.
- PCA9532 주소로 오는 쓰기는 ACK 후 LS 값을 시리얼로 그대로 출력. LED 상태를 호스트가 알 수 있게 한다.
- 모든 트랜잭션에 millis 타임스탬프를 붙여 폴링 주기를 계산할 수 있게 한다.

### 코드 규약

CommonClaude의 MIT 컨벤션을 따른다. snake_case, 80컬럼, 4칸 들여쓰기, 영어 Doxygen 주석, 매직 넘버 금지. 레지스터 주소와 타이밍은 `#define` 상수로 둔다.

## 5. CommonClaude 하네스 적용

[CommonClaude](https://github.com/coport-uni/CommonClaude)는 모든 Claude Code 세션이 따르는 규약과 훅을 담은 저장소다. 이 프로젝트에서는 `CLAUDE.md`, `.claude/`, `.clang-format`을 그대로 가져오고, ToDo.md와 GitHub 이슈 기반 작업 관리를 따른다.

### 저장소 구성

| 파일 | 역할 | 이 프로젝트 적용 |
| --- | --- | --- |
| `CLAUDE.md` | 세션 규칙 본문, 검증 게이트 §5.1 | 그대로 복사 |
| `.claude/settings.json` | 훅 설정 | 그대로 복사 |
| `.claude/hooks/pre-write-guard.sh` | `tests/`에 debug_, scratch_, tmp_, experiment_ 파일 쓰기 차단 | 적용 |
| `.claude/hooks/post-write-lint.sh` | C 파일 저장 시 clang-format과 cppcheck 실행 | .ino와 .cpp에 적용 |
| `.claude/hooks/post-write-debug-remind.sh` | `claude_test/` 추가 시 README 갱신 알림 | 적용 |
| Stop 훅 | 종료 전 ToDo.md 항목과 GitHub 이슈 존재 확인 | 적용 |
| `.clang-format` | LLVM 기반, 80컬럼, 4칸 | 적용 |
| `CLAUDECowork.md` | Cowork 세션 규칙, 경비 보고서와 메일 | 미적용 |
| `ClaudeMetal.md` | meviy 절삭 설계 가이드 | 미적용 |
| `ubuntu2404-setup.sh` | 컨테이너 환경 설치 | 호스트 PC 초기 설정에 사용 |

### 작업 워크플로, README §3 기준

1. 입력 검증: 명령이 명확한지, 참조 자료가 있는지 확인. 이 사양서가 참조 자료다.
2. `ToDo.md` 작성: 추가만 하고 삭제하지 않는다.
3. 사용자 승인.
4. `gh issue create`로 GitHub 이슈 등록.
5. 실행, ToDo.md 항목 체크.
6. 검증: 하드웨어 코드는 실제 UNO Q와 키패드 보드에서 실행해 확인. 테스트와 dry run은 대체 불가.
7. `gh issue edit`로 진행 상황 동기화.

### 검증 게이트 적용 방식

| 산출물 | 검증 방법 | PR Testing 섹션에 붙일 실제 출력 |
| --- | --- | --- |
| `i2c_scan` | UNO Q에 키패드 연결 후 실행 | `ADDR` 3줄 |
| `pca9555_poll` | 18개 버튼 실물 누름 | `CHG` 로그 18개 이상 |
| `pca9532_led` | 16개 LED 점등 확인 | 명령과 `OK` 응답, 사진 |
| `pca9555_emu` | 메인보드 연결 후 Start 키 주입 | 스핀코터 모터 동작 로그와 영상 |

미검증 코드는 커밋, 푸시, PR, 머지를 하지 않는다. 복잡한 작업이나 플랜 모드에서는 명령 끝에 `ultrathink`를 붙인다.

### 디렉터리 규칙

| 경로 | 용도 |
| --- | --- |
| `tests/` | CI용 정식 테스트, 호스트 측 파서와 매핑 JSON 검증 |
| `claude_test/` | 일회성 실험 스케치, README.md에 목록 유지 |

## 6. 호스트 PC 개발 환경

호스트 PC에는 Arduino CLI와 UNO Q 코어, Python 3 시리얼 도구, gh CLI, clang-format, cppcheck가 필요하다. Claude Code는 컴파일과 업로드까지 실행하되, 하드웨어 실행 결과 확인은 사용자가 옆에서 한다.

### 툴체인

| 도구 | 용도 | 확인 명령 |
| --- | --- | --- |
| arduino-cli | 컴파일, 업로드, 시리얼 모니터 | `arduino-cli version` |
| UNO Q 보드 코어 | STM32U585 Zephyr 코어 | `arduino-cli core list` |
| Python 3, pyserial | 로그 파서, 매핑 JSON 생성, 키 주입 클라이언트 | `python3 -m serial.tools.list_ports` |
| gh | 이슈 생성과 갱신 | `gh auth status` |
| clang-format, cppcheck | post-write-lint 훅 | `clang-format --version` |

### 저장소 구조

```text
spincoater-keypad/
  CLAUDE.md              CommonClaude에서 복사
  .claude/               훅과 settings.json
  .clang-format
  ToDo.md
  docs/
    spec.md              이 사양서
    pinmap.md            도통 측정 결과
    button_map.json      주소, 레지스터, 비트, 버튼명
    led_map.json         PCA9532 LS 인덱스와 LED명
  firmware/
    i2c_scan/
    pca9555_poll/
    pca9532_led/
    pca9555_emu/
  host/
    log_parser.py        CHG, RX 로그를 JSON으로
    keypad_client.py     PRESS, HOLD, RELEASE 전송
  tests/                 파서와 JSON 스키마 테스트
  claude_test/           일회성 실험, README.md 목록
```

### 세션 워크플로

1. `git clone` 후 CommonClaude의 `CLAUDE.md`, `.claude/`, `.clang-format`을 복사한다.
2. Claude Code를 저장소 루트에서 실행하고 이 사양서를 참조 자료로 지정한다.
3. 작업 단위마다 ToDo.md 항목과 GitHub 이슈를 만든다. 예: `i2c_scan 스케치 작성과 UNO Q 실행 확인`.
4. Claude Code가 `arduino-cli compile`과 `upload`를 실행하고 시리얼 출력을 캡처한다.
5. 사용자가 버튼 누름과 LED 점등을 눈으로 확인하고 결과를 알려 준다.
6. 실제 출력을 PR의 Testing 섹션에 붙이고 이슈를 갱신한다.

### 시리얼 포트

UNO Q는 USB C 하나로 Linux 측과 MCU 측을 함께 노출한다. `list_ports`로 잡히는 포트 중 STM32 측 CDC를 골라 `--port`에 지정하고, 포트명은 `.env` 또는 `config.json`에 두어 스케치와 스크립트가 공유한다.

## 7. 산출물과 검증 기준

| 산출물 | 형식 | 완료 기준 | 검증 |
| --- | --- | --- | --- |
| `docs/pinmap.md` | 표 | 헤더 5핀 신호 확정, R16 직렬 여부 판정, 주소 3개 계산값 | 멀티미터 측정값 기재 |
| `firmware/i2c_scan` | .ino | 주소 3개 출력 | 실기 시리얼 캡처 |
| `docs/button_map.json` | JSON | S1 ~ S18 전부 주소, 레지스터, 비트 기재 | 각 버튼 2회 이상 재현 |
| `docs/led_map.json` | JSON | D1 ~ D18 전부 칩과 인덱스 기재 | 점등 사진 |
| `firmware/pca9555_emu` | .ino | 메인보드 폴링에 NAK 없이 응답, 키 주입으로 Start 동작 | 스핀코터 동작 영상, RX 로그 |
| `host/keypad_client.py` | Python | `press S1` 한 줄로 스핀코터 조작 | 실기 동작 확인 |
| 트랜잭션 로그 | 텍스트 | 폴링 주기 ms, 레지스터 순서, 부팅 시 초기화 시퀀스 | 30초 이상 캡처 |

모든 하드웨어 산출물은 CommonClaude 검증 게이트에 따라 실기 출력이 PR에 첨부되어야 머지된다.

## 8. 미결 사항과 리스크

| 항목 | 상태 | 영향 | 대응 |
| --- | --- | --- | --- |
| SCL 도통 불가 원인 | 미확인 | 배선 오류 시 스캔 실패 | 저항 모드로 R16 직렬 여부 측정 |
| 헤더 5번 핀 신호 | 미확인, INT 또는 RESET | RESET이면 에뮬레이터가 이 핀을 HIGH로 유지해야 함 | PCA9555 1번, PCA9532 21번과 도통 |
| 메인보드 버스 전압 | 미측정 | 5 V이면 UNO Q 직결 불가 | 메인보드 헤더 VDD 전압 측정, BSS138 준비 |
| UNO Q Wire 슬레이브 지원 | 미검증 | 2단계 보드 교체 필요 | 두 UNO Q 또는 UNO Q와 Nano로 슬레이브 테스트 |
| 메인보드 키 인식 최소 시간 | 미확인 | 주입 무시 가능 | 100 ms부터 50 ms 단위 증가 |
| 메인보드 부팅 시 초기화 시퀀스 | 미확인 | 설정 레지스터 쓰기에 응답 못 하면 에러 | 부팅 직후 로그 캡처 |
| PCA9532 NAK 처리 | 미확인 | LED 주소 미응답 시 메인보드 에러 가능 | 에뮬레이터가 3주소 모두 ACK |
| Ellenby 회로도 | 없음 | 모든 핀맵을 측정으로 확정 | 09-0128-00 문의 메일 1회 시도 |

## Sources

- [NXP PCA9555 데이터시트](https://www.nxp.com/docs/en/data-sheet/PCA9555.pdf)
- [NXP PCA9532 데이터시트](https://www.nxp.com/docs/en/data-sheet/PCA9532.pdf)
- [Arduino UNO Q Power Specifications](https://docs.arduino.cc/tutorials/uno-q/power-specification/)
- [Arduino UNO Q Getting Started, Visuino](https://www.visuino.com/arduino-uno-q-getting-started/)
- [coport-uni/CommonClaude](https://github.com/coport-uni/CommonClaude)
