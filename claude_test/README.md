# claude_test

일회성 실험 스케치와 실측 로그 목록. 정식 산출물은 `firmware/`에 둔다.

## 스케치

| 스케치 | 목적 | 상태 |
| --- | --- | --- |
| `keypad_probe` | 키패드 보드 1차 하드웨어 응답 확인. PCA9555 설정 레지스터 0x06, 0x07 읽기, 입력 레지스터 0x00, 0x01 50 ms 폴링, D2 INT 에지 보고. 2차에서 PCA9532 입력 레지스터까지 48비트로 확장 | 2026-09-29 UNO Q에서 실행 완료. I2C 슬레이브 3개 응답, 키 입력 레벨 유지, D2 INT 에지 모두 확인 |
| `bus_check` | `Wire`를 전혀 쓰지 않고 SDA·SCL·INT의 유휴 레벨만 읽는다. 하이임피던스와 내부 풀업 두 조건으로 샘플링해 **외부 풀업 있음 / 플로팅 / 누군가 로우로 잡고 있음**을 구분한다 | 2026-09-29 작성. R4 Minima 실행 대기 |

## 실측 로그

| 로그 | 내용 |
| --- | --- |
| `keypad_probe/button_map_pass1.log` | 1차 버튼 매핑. 18개를 순서대로 눌러 17개 검출, CHG 36줄. VACUUM만 미검출이며 13번과 14번 사이 간격이 6,527 ms로 한 사이클 통째로 비어 그 자리가 VACUUM임을 특정. `docs/button_map.json`의 근거 |
| `keypad_probe/button_map_pass2_vacuum.log` | 2차. PCA9532 입력 레지스터까지 감시 범위를 넓힌 뒤 VACUUM을 3회 눌러 1회 검출. `0x21` 레지스터 `0x00` 비트 6으로 확정. 돔 스위치 접촉 불량도 이 로그에 남아 있음 |
| `keypad_probe/r4_minima_led_check.log` | UNO R4 Minima + 키패드 5 V 구동에서 `firmware/pca9532_led` 실행 시도. **실패 기록**이며 성공 증거가 아니다. 명령은 온전히 도착했으나 I2C 쓰기가 전부 NACK |

## 시리얼 읽는 법이 보드마다 다르다

### UNO Q

MCU의 `Serial`은 호스트로 직접 나오지 않는다. Router Bridge의 `Monitor` 객체가
Linux 측 `127.0.0.1:7500`으로 나가고, `arduino-router-serial.service`가 이를
`/dev/ttyGS0`으로 중계한다. 호스트 COM 포트에서 안 읽히면 adb로 소켓을 직접 읽는다.

```sh
ADB="$LOCALAPPDATA/Arduino15/packages/arduino/tools/adb/32.0.0/adb.exe"
"$ADB" shell "nc 127.0.0.1 7500"
```

`setup()` 출력은 부팅 직후 한 번만 나오므로, 캡처를 먼저 걸고 재업로드해서
리셋시켜야 초기 레지스터 덤프를 놓치지 않는다.

### UNO R4 Minima

보통의 USB CDC라 COM 포트로 바로 읽힌다. 단 **포트를 여는 순간 DTR이 보드를
리셋**시킨다. 열자마자 명령을 보내면 부팅 배너에 삼켜져 전부 `ERR`가 되므로,
포트를 연 뒤 2.5초 정도 기다렸다가 보낸다.

## 빌드와 업로드

```sh
CLI="/c/Program Files/Arduino IDE/resources/app/lib/backend/resources/arduino-cli.exe"

# UNO Q
"$CLI" compile --fqbn arduino:zephyr:unoq         claude_test/keypad_probe
"$CLI" upload  --fqbn arduino:zephyr:unoq -p COM17 claude_test/keypad_probe

# UNO R4 Minima
"$CLI" compile --fqbn arduino:renesas_uno:minima         claude_test/bus_check
"$CLI" upload  --fqbn arduino:renesas_uno:minima -p COM18 claude_test/bus_check
```

R4가 업로드 후 DFU 모드에 머물러 COM 포트로 안 돌아오는 일이 잦다. 그때는
`-p 1-9`처럼 DFU 포트를 지정해 다시 올린다. `Wire`를 쓰는 스케치에서 특히
자주 발생하는데, 버스가 비정상일 때 Renesas `Wire`가 기동 중에 멈추는 것으로
보인다.
