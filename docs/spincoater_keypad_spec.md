# Spin coater keypad I2C reverse-engineering specification

Written 2026-09-29.

> This is the specification of record, describing the plan and what was
> known on the day it was written. Items marked unconfirmed here may
> since have been settled; the current state of the project is in
> [../README.md](../README.md).

## 1. Goal and scope

Analyse the I2C interface of spin coater keypad board 09-0128-00 with an
Arduino UNO Q, and ultimately build a PCA9555 slave emulator that
injects key presses into the mainboard. Development runs in Claude Code
on the CommonClaude harness, on the host PC the UNO Q is attached to.

| Stage | Goal | Done when |
| --- | --- | --- |
| 1. Explore | Obtain the keypad board addresses, the button bitmap and the LED map | The mapping table for 18 buttons and 18 LEDs is 100% written |
| 2. Record | Record the I2C transactions the mainboard sends to the keypad | Polling period in ms, register order and the LED command log are captured |
| 3. Emulate | Have the MCU stand in for the keypad board and inject key presses into the mainboard | Start and Stop of the spin coater succeed from a PC command |

Out of scope: the LCD data path, the motor driver, and any modification
of the mainboard firmware.

## 2. Hardware as found

The keypad board consists of three I2C slaves and three debouncers, and
reaches the mainboard through a single five-pin header, X1. The LCD runs
to the mainboard on its own FPC and is out of scope.

| Position | Part | Role | Address range | Operating voltage |
| --- | --- | --- | --- | --- |
| U4, U6 | [PCA9555D](https://www.nxp.com/docs/en/data-sheet/PCA9555.pdf) | 16-bit I2C GPIO expander, button input | 0x20 – 0x27 | 2.3 – 5.5 V |
| U5 | [PCA9532D](https://www.nxp.com/docs/en/data-sheet/PCA9532.pdf) | 16-bit I2C LED dimmer, 2 PWM channels | 0x60 – 0x67 | 2.3 – 5.5 V |
| U1, U2, U3 | MAX6818EAP | 8-channel switch debouncer, about 40 ms | none | 2.7 – 5.5 V |

### Pins to measure

| Signal | PCA9555D | PCA9532D | MAX6818EAP |
| --- | --- | --- | --- |
| VDD | 24 | 24 | 20 |
| VSS, GND | 12 | 12 | 9 |
| SDA | 23 | 23 | none |
| SCL | 22 | 22 | none |
| INT, CH | 1 | none | 11 |
| RESET | none | 21 | none |

Pin 1 is on the side with the dot or the bevel. With the dot at the top
left, the left column numbers downward and the right column numbers
upward. The PCA9532D has no dot, so start from the two sides of
capacitor C3 and check continuity to the four corner pins to fix pin 24
VDD and pin 12 VSS first.

### Established by continuity testing, 2026-09-29

- VDD, GND and SDA on header X1 have direct continuity to the chip pins.
- SCL does not buzz. R16, next to the header, is presumed to be 1 kΩ in
  series; confirm by measuring in resistance mode.
- Board silkscreen: 09-0128-00 Rev, Copyright 2005 Ellenby Technologies.
  No public schematic exists.

### Presumed pinout of header X1

| Pin | Signal | State |
| --- | --- | --- |
| 1 | VDD | continuity confirmed |
| 2 | GND | continuity confirmed |
| 3 | SDA | continuity confirmed |
| 4 | SCL | presumed via R16 |
| 5 | INT or RESET | unconfirmed |

## 3. What the UNO Q and the device have to do

The UNO Q is 3.3 V logic, so the keypad board is driven from the UNO Q's
3.3 V rail and explored without a level shifter. Only the mainboard
connection stage needs 5 V tolerance.

### UNO Q electrical conditions

| Item | Value | Source |
| --- | --- | --- |
| MCU logic voltage | 3.3 V | [UNO Q Power Specifications](https://docs.arduino.cc/tutorials/uno-q/power-specification/) |
| Header absolute maximum | 3.6 V | same document |
| Default I2C pins | D20 SDA, D21 SCL, beside AREF | [UNO Q Getting Started](https://www.visuino.com/arduino-uno-q-getting-started/) |
| Sketch runtime | Zephyr-based Arduino Core on the STM32U585 | same document |

### Stage 1 wiring, keypad board standalone

| UNO Q | Keypad X1 | Note |
| --- | --- | --- |
| 3.3V | VDD | never use the 5V pin |
| GND | GND | connect GND first |
| D20 SDA | SDA | |
| D21 SCL | SCL | leave R16, 1 kΩ in series, in place |
| D2 | INT | optional |

Add a 4.7 kΩ pull-up to 3.3 V only if the resistance between the board's
SDA and VDD measures infinite.

### Steps

1. With the power off, measure in resistance mode between header pin 4
   and PCA9555 pin 22 to settle whether R16 is in series. 900 Ω to
   1,100 Ω means it is.
2. Check continuity of address pins A0, A1 and A2 to VDD or GND and
   compute the addresses of U4, U6 and U5. PCA9555 is
   0x20 + 4·A2 + 2·A1 + A0; PCA9532 is 0x60 + 4·A2 + 2·A1 + A0.
3. Drive the keypad board from the UNO Q's 3.3 V and confirm three
   addresses with an I2C scanner.
4. Read PCA9555 configuration registers 0x06 and 0x07 to tell input pins
   from output pins.
5. Hold each of S1 to S18 for at least 2 s and record the address and
   bit with a 50 ms poll. The input registers are level based, so the
   bit stays at 0 for as long as the key is held.
6. Write values to PCA9532 LS0 through LS3 to map D1 through D16, and
   find the remaining two LEDs among the PCA9555 output bits.
7. Stage 2: fit the slave emulator to the mainboard in place of the
   keypad board and capture the transaction log. If the mainboard bus is
   5 V, insert a BSS138 level shifter.
8. Stage 3: send key injection commands from the host PC over serial,
   following the polling order the log establishes.

### Board choice for stages 2 and 3

| Candidate | Advantage | Disadvantage |
| --- | --- | --- |
| UNO Q | already in hand, log processing possible on the Linux side | Zephyr core's `Wire` slave support unverified, 3.3 V |
| Three Nanos | `Wire` slave support proven, direct 5 V | one board per address |
| ESP32 | slave support, logging over WiFi | 3.3 V, single address |

Start stage 1 on the UNO Q and, before stage 2, run a separate test to
decide whether UNO Q slave mode is usable.

## 4. Firmware specification

The firmware is split into four sketches, all speaking a one-line text
protocol with the host PC at 115200 bps. Mapping results are not
hard-coded into the firmware; they live in JSON files on the host side.

| Sketch | Role | Input | Output |
| --- | --- | --- | --- |
| `i2c_scan` | bus address scan | none | `ADDR 0x20`, `ADDR 0x21`, `ADDR 0x60` form |
| `pca9555_poll` | read 0x00 and 0x01 of both PCA9555s every 50 ms and print only the changed bits | none | `CHG addr reg bit val ms` |
| `pca9532_led` | write values to the LS registers | `LED n on/off/pwm0/pwm1` | `OK` or `ERR` |
| `pca9555_emu` | slave emulator, mainboard transaction log, key injection | `PRESS n ms`, `HOLD n`, `RELEASE n` | `RX addr reg data ms` |

### Register reference

| Chip | Register | Meaning |
| --- | --- | --- |
| PCA9555 | 0x00, 0x01 | input ports 0 and 1, read only |
| PCA9555 | 0x02, 0x03 | output ports 0 and 1 |
| PCA9555 | 0x06, 0x07 | configuration, bit 1 means input |
| PCA9532 | 0x02, 0x03 | PSC0, PWM0 |
| PCA9532 | 0x06 – 0x09 | LS0 – LS3, two bits per LED: 00 off, 01 on, 10 PWM0, 11 PWM1 |
| PCA9532 | control byte bit 4 | auto increment |

### Emulator requirements

- Three addresses answering at once. Either three parallel Nanos, or an
  AVR TWAMR set to 0xFE with a custom TWI ISR that identifies the
  address from TWDR.
- Input registers default to 0xFF; an injected key holds its bit at 0.
  Default hold time 100 ms, adjustable by command.
- The INT pin drops LOW when an input register changes and returns HIGH
  once the mainboard reads it, identical to the PCA9555 datasheet
  behaviour.
- Writes arriving at the PCA9532 address are ACKed and the LS value is
  echoed to serial verbatim, so the host can track the LED state.
- Every transaction carries a `millis` timestamp so the polling period
  can be computed.

### Code convention

Follow the CommonClaude MIT convention: snake_case, 80 columns, 4-space
indent, English Doxygen comments, no magic numbers. Register addresses
and timings go in `#define` constants.

## 5. Applying the CommonClaude harness

[CommonClaude](https://github.com/coport-uni/CommonClaude) is the
repository of conventions and hooks every Claude Code session follows.
This project takes `CLAUDE.md`, `.claude/` and `.clang-format` from it
as they are, and follows its ToDo.md and GitHub-issue-based task
management.

### Repository contents

| File | Role | Applied here |
| --- | --- | --- |
| `CLAUDE.md` | the ruleset itself, including the §5.1 verification gate | copied as is |
| `.claude/settings.json` | hook configuration | copied as is |
| `.claude/hooks/pre-write-guard.sh` | blocks writing debug_, scratch_, tmp_, experiment_ files into `tests/` | applied |
| `.claude/hooks/post-write-lint.sh` | runs clang-format and cppcheck when a C file is saved | applied to .ino and .cpp |
| `.claude/hooks/post-write-debug-remind.sh` | reminds to update the README when `claude_test/` gains a file | applied |
| Stop hook | checks a ToDo.md entry and a GitHub issue exist before exit | applied |
| `.clang-format` | LLVM base, 80 columns, 4 spaces | applied |
| `CLAUDECowork.md` | Cowork session rules, expense reports and mail | not applied |
| `ClaudeMetal.md` | meviy machining design guide | not applied |
| `ubuntu2404-setup.sh` | container environment install | used for host PC initial setup |

### Workflow, per README §3

1. Input validation: is the command explicit, and are there reference
   materials? This specification is the reference material.
2. Write `ToDo.md`: append only, never delete.
3. User confirmation.
4. Register a GitHub issue with `gh issue create`.
5. Do the work and tick off the `ToDo.md` items.
6. Verify: hardware code is confirmed by running it on the real UNO Q
   and keypad board. Tests and dry runs are not a substitute.
7. Sync progress with `gh issue edit`.

### How the verification gate applies

| Artifact | Verification method | Real output to paste into the PR Testing section |
| --- | --- | --- |
| `i2c_scan` | run with the keypad connected to the UNO Q | three `ADDR` lines |
| `pca9555_poll` | physically press all 18 buttons | 18 or more `CHG` lines |
| `pca9532_led` | confirm all 16 LEDs light | the commands with their `OK` replies, plus a photograph |
| `pca9555_emu` | inject a Start key with the mainboard connected | spin coater motor running, log and video |

Unverified code is never committed, pushed, proposed or merged. For
complex work, or in plan mode, append `ultrathink` to the command.

### Directory rules

| Path | Purpose |
| --- | --- |
| `tests/` | formal CI tests: host-side parser and mapping JSON validation |
| `claude_test/` | one-off experimental sketches, listed in its README.md |

## 6. Host PC development environment

The host PC needs the Arduino CLI and the UNO Q core, Python 3 serial
tooling, the `gh` CLI, clang-format and cppcheck. Claude Code runs
compile and upload; the operator confirms the hardware result at the
bench.

### Toolchain

| Tool | Purpose | Check command |
| --- | --- | --- |
| arduino-cli | compile, upload, serial monitor | `arduino-cli version` |
| UNO Q board core | STM32U585 Zephyr core | `arduino-cli core list` |
| Python 3, pyserial | log parser, mapping JSON generation, key injection client | `python3 -m serial.tools.list_ports` |
| gh | issue creation and updates | `gh auth status` |
| clang-format, cppcheck | the post-write-lint hook | `clang-format --version` |

### Repository structure

```text
spincoater-keypad/
  CLAUDE.md              copied from CommonClaude
  .claude/               hooks and settings.json
  .clang-format
  ToDo.md
  docs/
    spec.md              this specification
    pinmap.md            continuity measurement results
    button_map.json      address, register, bit, button name
    led_map.json         PCA9532 LS index and LED name
  firmware/
    i2c_scan/
    pca9555_poll/
    pca9532_led/
    pca9555_emu/
  host/
    log_parser.py        CHG and RX logs to JSON
    keypad_client.py     sends PRESS, HOLD, RELEASE
  tests/                 parser and JSON schema tests
  claude_test/           one-off experiments, listed in README.md
```

### Session workflow

1. After `git clone`, copy `CLAUDE.md`, `.claude/` and `.clang-format`
   from CommonClaude.
2. Run Claude Code at the repository root and point it at this
   specification as reference material.
3. Create a ToDo.md entry and a GitHub issue per unit of work, for
   example "write the `i2c_scan` sketch and confirm it on the UNO Q".
4. Claude Code runs `arduino-cli compile` and `upload` and captures the
   serial output.
5. The operator confirms the key presses and LED lighting by eye and
   reports the result.
6. Paste the real output into the PR's Testing section and update the
   issue.

### Serial port

The UNO Q exposes both the Linux side and the MCU side over a single
USB-C. Pick the STM32-side CDC among the ports `list_ports` reports and
pass it as `--port`. Keep the port name in `.env` or `config.json` so
sketches and scripts share it.

## 7. Deliverables and acceptance criteria

| Deliverable | Format | Done when | Verification |
| --- | --- | --- | --- |
| `docs/pinmap.md` | table | the five header signals are settled, R16 series is decided, the three addresses are computed | multimeter readings recorded |
| `firmware/i2c_scan` | .ino | three addresses printed | real serial capture |
| `docs/button_map.json` | JSON | S1 – S18 all carry address, register and bit | each button reproduced at least twice |
| `docs/led_map.json` | JSON | D1 – D18 all carry chip and index | photograph of them lit |
| `firmware/pca9555_emu` | .ino | answers the mainboard's polling without a NAK, injected key starts the machine | spin coater video and RX log |
| `host/keypad_client.py` | Python | `press S1` on one line operates the spin coater | confirmed on the real machine |
| Transaction log | text | polling period in ms, register order, boot-time init sequence | at least 30 s captured |

Per the CommonClaude verification gate, no hardware deliverable merges
without its real output attached to the PR.

## 8. Open items and risks

| Item | State | Impact | Response |
| --- | --- | --- | --- |
| Why SCL shows no continuity | unconfirmed | a wiring error would make the scan fail | measure in resistance mode for R16 in series |
| Signal on header pin 5 | unconfirmed, INT or RESET | if RESET, the emulator must hold this pin HIGH | check continuity to PCA9555 pin 1 and PCA9532 pin 21 |
| Mainboard bus voltage | unmeasured | at 5 V the UNO Q cannot be connected directly | measure VDD at the mainboard header, have a BSS138 ready |
| UNO Q `Wire` slave support | unverified | stage 2 may need a different board | slave test with two UNO Qs, or a UNO Q and a Nano |
| Minimum key hold time the mainboard accepts | unconfirmed | injection may be ignored | start at 100 ms and increase in 50 ms steps |
| Mainboard boot-time init sequence | unconfirmed | failing to answer a configuration write raises an error | capture the log immediately after boot |
| PCA9532 NAK handling | unconfirmed | no answer at the LED address may fault the mainboard | the emulator ACKs all three addresses |
| Transfers lost after an even command byte | fixed 2026-10-06 in `pca9555_emu_gui_mk2`, #18/#21/#22 | `LS0`/`LS2` data and the port-0 reads never reach the emulator, so two lamps stay dark and the port-0 keys likely do nothing | the target's `SDADEL` 12 (400 kHz default) falls in a failing window around 250 to 410 ns; `SDADEL` 4 delivers every transfer. Applied in `pca9555_emu_gui_mk2`; its lamps match the real keypad and `PGDN` works |
| Ellenby schematic | none | every pinout must be settled by measurement | one attempt at an enquiry email about 09-0128-00 |

## Sources

- [NXP PCA9555 datasheet](https://www.nxp.com/docs/en/data-sheet/PCA9555.pdf)
- [NXP PCA9532 datasheet](https://www.nxp.com/docs/en/data-sheet/PCA9532.pdf)
- [Arduino UNO Q Power Specifications](https://docs.arduino.cc/tutorials/uno-q/power-specification/)
- [Arduino UNO Q Getting Started, Visuino](https://www.visuino.com/arduino-uno-q-getting-started/)
- [coport-uni/CommonClaude](https://github.com/coport-uni/CommonClaude)
