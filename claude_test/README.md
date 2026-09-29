# claude_test

일회성 실험 스케치 목록. 정식 산출물은 `firmware/`에 둔다.

| 스케치 | 목적 | 상태 |
| --- | --- | --- |
| `keypad_probe` | 키패드 보드 1차 하드웨어 응답 확인. PCA9555 설정 레지스터 0x06, 0x07 읽기, 입력 레지스터 0x00, 0x01 50 ms 폴링, D2 INT 에지 보고 | 2026-09-29 실행 완료. I2C 슬레이브 3개 응답, 키 입력 레벨 유지, D2 INT 에지 모두 확인 |

## keypad_probe 실행 방법

UNO Q의 MCU `Serial`은 호스트로 직접 나오지 않는다. Router Bridge의 `Monitor`
객체가 Linux 측 `127.0.0.1:7500`으로 나가고, `arduino-router-serial.service`가
이를 `/dev/ttyGS0`으로 중계한다. 호스트 COM 포트에서 읽히지 않을 때는 adb로
Linux 측 소켓을 직접 읽는다.

```sh
ARDUINO15="$LOCALAPPDATA/Arduino15/packages/arduino/tools"
ADB="$ARDUINO15/adb/32.0.0/adb.exe"
CLI="/c/Program Files/Arduino IDE/resources/app/lib/backend/resources/arduino-cli.exe"

"$CLI" compile --fqbn arduino:zephyr:unoq claude_test/keypad_probe
"$CLI" upload  --fqbn arduino:zephyr:unoq -p COM17 claude_test/keypad_probe

# setup() 출력은 부팅 직후 한 번만 나오므로 캡처를 먼저 걸고 재업로드한다.
"$ADB" shell "nohup timeout 900 nc 127.0.0.1 7500 > /tmp/probe.log 2>&1 &"
"$ADB" shell "cat /tmp/probe.log"
```
