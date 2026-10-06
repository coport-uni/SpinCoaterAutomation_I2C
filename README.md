# SpinCoaterAutomation_I2C

Reverse-engineering the keypad board of a Laurell spin coater
(Ellenby Technologies 09-0128-00) over I2C, and then replacing that
keypad with an emulator so the machine can be driven from a host PC.
The specification is
[docs/spincoater_keypad_spec.md](docs/spincoater_keypad_spec.md).

The machine is not modified. Its mainboard keeps talking to what it
believes is the original keypad.

```mermaid
flowchart TB
    subgraph ORIG["As built"]
        direction LR
        KP["<b>Keypad 09-0128-00</b><br/>18 buttons, 18 LEDs<br/>PCA9555 0x21 and 0x22<br/>PCA9532 0x60"]
        MB1["<b>Spin coater mainboard</b><br/>I2C master, polls the keypad"]
        KP <-->|"header X1<br/>SDA · SCL · INT"| MB1
    end

    subgraph NEW["What this project builds"]
        direction LR
        PC["<b>Host PC</b><br/>press START,<br/>read back which keys are legal"]
        UQ["<b>Arduino UNO Q</b><br/>slave emulator, answers<br/>0x21 · 0x22 · 0x60"]
        LS["<b>BSS138</b><br/>3.3 V to 5 V"]
        MB2["<b>Spin coater mainboard</b><br/>unchanged, unaware"]
        PC -->|USB serial| UQ
        UQ <--> LS
        LS <-->|"5 V I2C"| MB2
    end

    ORIG ~~~ NEW

    classDef orig fill:#eceff3,stroke:#9aa3b0,color:#11161d
    classDef new fill:#d6e6f7,stroke:#2f6ea8,color:#11161d
    class KP,MB1 orig
    class PC,UQ,LS,MB2 new
```

The keypad is the only part of the machine that has to be understood:
it is a plain I2C peripheral behind a five-pin header, so an emulator
that answers the same three addresses is indistinguishable from it.

## Status

All three stages have now been exercised on the real machine. The
emulator has replaced the keypad board and moved the menu cursor.

```mermaid
flowchart LR
    S1["<b>1 · Explore</b><br/>addresses, button bitmap, LED map<br/><i>done 2026-09-29</i>"]
    S2["<b>2 · Record</b><br/>log what the mainboard sends<br/><i>done 2026-10-06</i>"]
    S3["<b>3 · Emulate</b><br/>inject keys, drive the coater<br/><i>arrows verified 2026-10-06</i>"]
    S1 --> S2 --> S3

    classDef done fill:#d8eedd,stroke:#2f7d55,color:#11161d
    classDef part fill:#fdf0d0,stroke:#a8821f,color:#11161d
    class S1,S2 done
    class S3 part
```

Stage 1 ran with the keypad board detached from the mainboard and
powered off the Arduino UNO Q's 3.3 V rail; all 18 buttons and all 16
PCA9532 LEDs were mapped there. For stages 2 and 3 the keypad was
unplugged for good and the UNO Q took its place on the mainboard bus
through a PCA9306 translator. The mainboard boots normally against the
emulator and the up and down arrows move the "Select Process" cursor.

Stage 3 is **partial**: only the two arrow keys are implemented, and the
firmware refuses every other key by name. Nothing yet drives the chuck.

## Hardware

The keypad reaches the mainboard through one five-pin header, X1. The
LCD is a separate FPC straight to the mainboard and is out of scope.

| Position | Part | Role | I2C address |
| --- | --- | --- | --- |
| U4 | PCA9555D | 16 buttons | `0x21` |
| U6 | PCA9555D | 2 buttons, 2 LEDs (inferred) | `0x22` |
| U5 | PCA9532D | 16 LEDs | `0x60` |
| U1–U3 | MAX6818EAP | switch debouncers, about 40 ms | none |

### Header X1

| Pin | Signal | Evidence |
| --- | --- | --- |
| 1 | VDD | continuity |
| 2 | GND | continuity |
| 3 | SDA | continuity |
| 4 | SCL | R16, 1 kΩ, in series. Proven by a working 100 kHz bus |
| 5 | **INT** | measured, confirmed. Not RESET |

### Stage 1 bench wiring, keypad standalone

| UNO Q | Keypad X1 |
| --- | --- |
| 3.3V | VDD |
| GND | GND |
| D20 (SDA) | SDA |
| D21 (SCL) | SCL |
| D2 | INT |

> The UNO Q header's absolute maximum is 3.6 V. Never use the 5V pin.

## What the keypad turned out to be

![Keypad 09-0128-00 with every key annotated by its I2C register bit and PCA9532 LED channel](docs/diagrams/keypad_bitmap.svg)

A pressed key drives its bit to `0` and **holds it there for as long as
the key is down** — level, not edge. The full table is in
[docs/button_map.json](docs/button_map.json) and
[docs/led_map.json](docs/led_map.json); the panel photograph is
[docs/ButtonLayout.jpg](docs/ButtonLayout.jpg).

![Register map: PCA9555 input register bits carry the buttons, PCA9532 LS register fields drive the matching LEDs](docs/diagrams/register_map.svg)

The single most useful finding is the index symmetry: **PCA9532 channel
n drives the LED of the button at `0x21` bit n**, with input port 0
bits 0–7 as channels 0–7 and input port 1 bits 0–7 as channels 8–15.
Button and lamp share one index, so no separate lookup table is needed.

There are 18 LEDs and the PCA9532 has 16 channels. Header X1 carries
only VDD, GND, SDA, SCL and INT, so every LED must be driven over I2C;
all 16 pins of `0x21` are buttons; therefore **the EDIT MODE and RUN
MODE lamps can only be on `0x22`.** Which pins exactly is a stage 2
question, answered by watching the mainboard write.

### The LEDs are a state feedback channel

This machine lights a button **only while that button is a legal input
in the current state**. So the LS register values the mainboard writes
to `0x60` are a live list of the commands it will currently accept. An
emulator that logs those writes hands the host a closed loop.

```mermaid
sequenceDiagram
    autonumber
    participant H as Host PC
    participant E as UNO Q emulator
    participant M as Mainboard
    M->>E: read 0x21 input ports
    E-->>M: 0xFF 0xFF, nothing pressed
    M->>E: write 0x60 LS0..LS3
    E-->>H: LS snapshot = the legal command set
    H->>H: wait_until_valid(START)
    H->>E: press START
    Note over E: 0x21 P1 bit 5 held at 0,<br/>INT pulled LOW
    M->>E: read 0x21 input port 1
    E-->>M: bit 5 = 0
    Note over E: INT returns HIGH once read
    M->>E: write 0x60 LS0..LS3
    E-->>H: LS changed, so the key was accepted
```

That is why `host/keypad_client.py` should be written around
`wait_until_valid(START)` followed by `press START`, rather than a blind
`press START`. It buys three things: no command is sent that would be
ignored, a key press can be confirmed by the change in the lit set, and
waiting becomes state-based instead of a fixed delay.

### INT

Exactly as the PCA9555 datasheet describes. INT goes LOW on any input
change and returns HIGH by itself once an input register is read.
Holding a key down keeps INT HIGH; only the next change pulls it low
again.

### Hardware faults found

- **The VACUUM dome switch makes poor contact.** Three deliberate
  presses produced one detection, and a 2 s press registered for only
  488 ms. The first mapping pass missed it entirely. This does not
  affect emulation, which never goes through the physical switch.
- **The right arrow bounces.** One 123 ms bounce was observed despite
  the MAX6818 debouncer.

## Development environment

### What you need

| Tool | Note |
| --- | --- |
| Arduino IDE | its bundled `arduino-cli` is the one used here |
| `arduino:zephyr` core 1.0.0 | installs `adb` 32.0.0 as well; no separate platform-tools |
| `Arduino_RouterBridge` library | without it the build fails on an `#error` |
| `gh` CLI | issue and PR management |

```sh
CLI="/c/Program Files/Arduino IDE/resources/app/lib/backend/resources/arduino-cli.exe"
"$CLI" core install arduino:zephyr@1.0.0
"$CLI" lib install Arduino_RouterBridge
```

### Serial on the UNO Q, which is not obvious

The MCU's `Serial` does not reach the host PC. The path is:

```mermaid
flowchart LR
    SK["Sketch<br/>Monitor object"] --> RB["Router Bridge<br/>UART /dev/ttyHS1"]
    RB --> AR["arduino-router<br/>127.0.0.1:7500, Linux side"]
    AR --> SO["socat<br/>arduino-router-serial.service"]
    SO --> GS["/dev/ttyGS0"]
    GS -->|USB| HOST["Host COM port"]
    AR -.->|"adb shell nc 127.0.0.1 7500<br/>the reliable route"| HOST

    classDef mcu fill:#d6e6f7,stroke:#2f6ea8,color:#11161d
    classDef linux fill:#d8eedd,stroke:#2f7d55,color:#11161d
    classDef host fill:#f8e6c0,stroke:#a8761d,color:#11161d
    class SK,RB mcu
    class AR,SO,GS linux
    class HOST host
```

So a sketch must print to `Monitor`, **not** to `Serial`:

```c
#include <Arduino_RouterBridge.h>

void setup() {
    Serial.begin(115200);
    Bridge.begin();
    Monitor.begin(115200);
    while (!Monitor) { delay(500); }
    Monitor.println("hello");   // not Serial.println
}
```

`arduino-cli monitor -p COM17` on the host sometimes reads nothing. The
dependable route is to read the Linux-side socket over adb:

```sh
ADB="$LOCALAPPDATA/Arduino15/packages/arduino/tools/adb/32.0.0/adb.exe"
"$ADB" shell "nc 127.0.0.1 7500"
```

`setup()` output appears once, right after boot. Start the capture
first and re-upload to force a reset, or the initial register dump is
lost.

### Build and upload

```sh
"$CLI" compile --fqbn arduino:zephyr:unoq firmware/i2c_scan
"$CLI" upload  --fqbn arduino:zephyr:unoq -p COM17 firmware/i2c_scan
```

The port number differs per machine; `"$CLI" board list` reports it.
Upload works by the Linux side writing the STM32U585 over SWD, so no
bootloader button press is involved.

## Repository layout

```text
docs/
  spincoater_keypad_spec.md   the specification
  ButtonLayout.jpg            front panel photograph
  button_map.json             all 18 buttons
  led_map.json                16 mapped LEDs plus the inference for the other 2
  diagrams/                   SVG figures used by this README
firmware/
  i2c_scan/                   bus address scan
  pca9532_led/                LED control, including a walk mode for mapping
claude_test/
  keypad_probe/               button mapping probe and its bench logs
  bus_check/                  reads SDA, SCL and INT levels without using Wire
  slave_selftest/             proves the UNO Q works as an I2C target
  dual_target/                proves one controller can hold two addresses
```

### Firmware

| Sketch | Role | Status |
| --- | --- | --- |
| `i2c_scan` | bus address scan | verified on hardware |
| `pca9532_led` | LED control (`LED n on/off/pwm0/pwm1`, `ALL off`, `WALK ms`, `HALT`) | verified on hardware |
| `pca9555_poll` | button polling | not written; `claude_test/keypad_probe` covers it for now |
| `pca9555_emu` | slave emulator: answers `0x21`, `0x22` and `0x60` for the mainboard, logs its traffic, injects the arrow keys | verified on hardware 2026-10-06 |

`pca9532_led` refuses `ALL on` on purpose, to avoid lighting all 16
LEDs at once while the board is running off a 3.3 V bench supply.

`pca9555_emu` refuses every key except the two arrows, by name, in
`handle_line()`. START, STOP and VACUUM cannot be reached from the host
protocol at all — not because the host declines to ask, but because the
firmware will not serve the request. Its host commands are:

```
PRESS UP [ms]      hold the up arrow, default 100 ms, 20..1000
PRESS DOWN [ms]    hold the down arrow
RELEASE            release early
STATE              print the register files and the INT level
LOG ON | LOG OFF   log every read, not only the ones that changed
```

## The mainboard, measured 2026-10-06

With the keypad unplugged and `firmware/pca9555_emu` answering in its
place, the spin coater was powered on and its traffic recorded from the
first byte. This is the stage 2 data the specification asked for.

```mermaid
flowchart LR
    MB["<b>Spin coater mainboard</b><br/>sole I2C master · 5 V"]
    LS["<b>PCA9306</b><br/>VREF1 3.3 V · VREF2 5 V"]
    I2C2["<b>i2c2</b> · D20/D21<br/>OA1 <code>0x21</code> · OA2 <code>0x22</code>"]
    I2C3["<b>i2c3</b> · A4/A5<br/>OA1 <code>0x60</code>"]
    MB <--> LS
    LS <--> I2C2
    LS <--> I2C3

    classDef ext fill:#eceff3,stroke:#9aa3b0,color:#11161d
    classDef emu fill:#d8eedd,stroke:#2f7d55,color:#11161d
    class MB,LS ext
    class I2C2,I2C3 emu
```

A4 and A5 are jumpered to D20 and D21, so both controllers sit on the
one physical bus. All three addresses registered with `rc=0`.

### What it writes at power-on

Six writes, and then it never writes again unless something changes:

| Order | Address | Register | Value | Meaning |
| --- | --- | --- | --- | --- |
| 1 | `0x60` | `0x03` PWM0 | `0x00` | dimmer duty |
| 2 | `0x22` | `0x03` output 1 | `0x00` | |
| 3 | `0x22` | `0x07` config 1 | `0x00` | **port 1 is all outputs** |
| 4 | `0x60` | `0x09` LS3 | `0x10` | channel 14 on |
| 5 | `0x60` | `0x07` LS1 | `0x00` | channels 4–7 off |
| 6 | `0x22` | `0x03` output 1 | `0xFC` | **bits 0 and 1 driven low** |

Write 3 answers an open item. The mainboard declares all of `0x22`
port 1 as outputs and then pulls bits 0 and 1 low, which is how the two
LEDs that are not on the PCA9532 are driven. The buttons EDIT MODE and
RUN MODE are on `0x22` port **0**; their indicator LEDs are on `0x22`
port **1**, bits 0 and 1, active low.

It never writes `LS0` (`0x06`) or `LS2` (`0x08`), so PCA9532 channels
0–3 and 8–11 are never lit from a cold boot. The LED map of stage 1 was
built by driving all 16 channels from the Arduino, so it says which
channel reaches which lamp; it does not say the mainboard uses them all.

### How it reads

Only `0x21` is polled. `0x22` and `0x60` were each read twice during
start-up and never again.

| Measure | Value |
| --- | --- |
| Registers polled | `0x00` and `0x01` of `0x21`, as one pair |
| Rate | 40 register reads per second, steady |
| Period | **50 ms** per pair |
| Dropped log events over 150 s | 0 |

### INT is not required

The expanders' interrupt line has no channel left on a two-channel
PCA9306, so D2 was left unconnected and `INT_WIRED` is `0`. The 50 ms
poll is unconditional, so the mainboard sees a key purely from the input
register. A 120 ms hold is comfortably longer than one poll period and
was accepted every time.

Wiring INT straight across would be a fault, not a shortcut: the line is
pulled to 5 V on the mainboard side whenever it is released, and the
UNO Q's D2 does not tolerate that.

### The LED write confirms the cursor independently

Channel 15 is the up arrow's lamp. On row 1 it is dark, because there is
nowhere to go up; it lights as soon as the cursor leaves row 1.

| Cursor | `LS3` (`0x60` reg `0x09`) | Channel 14 | Channel 15 |
| --- | --- | --- | --- |
| row 1 | `0x10` | on | off |
| rows 2–4 | `0x50` | on | on |

So the mainboard's own LED traffic reports where the cursor is, without
the camera. That is the "list of keys you may press right now" channel
the operator noticed on the real keypad, read from the other side.

## Next steps

### ⚠️ The mainboard bus is 5 V — do not connect without a shifter

Measured 2026-09-29: **the mainboard I2C bus is 5 V.** The UNO Q header
is rated 3.6 V absolute maximum, so **a direct connection destroys the
board.** A BSS138-class level shifter is mandatory.

![Wiring the UNO Q to the 5 V mainboard bus through a BSS138 level shifter](docs/diagrams/level_shifter.svg)

| Shifter | Connects to |
| --- | --- |
| LV | UNO Q 3.3V |
| HV | mainboard 5 V |
| GND | common to both |
| LV1 / HV1 | D20 SDA / X1 SDA |
| LV2 / HV2 | D21 SCL / X1 SCL |
| LV3 / HV3 | D2 INT / X1 INT |

INT is open drain like SDA and SCL, so it fits a bidirectional channel
unchanged. **Mainboard VDD does not go to the UNO Q** — the Arduino is
powered over USB.

The shifter module carries pull-ups on both rails, which incidentally
fixes a second problem. The keypad pulls up SDA and INT but **not SCL**;
in the original machine the mainboard supplied that pull-up. The reason
this never showed up on the UNO Q is that Zephyr's pinctrl biases the
I2C pins with pull-ups. On other cores it does show up.

Bench work on the keypad alone needs no shifter: drive the keypad from
the UNO Q's 3.3 V rail, which is how all of stage 1 was done.

### Choosing the emulator board

The specification proposed three parallel Nanos or an AVR TWAMR hack to
answer three addresses at once. **Neither is needed.** The UNO Q was
measured working as a slave. The evidence:

- `CONFIG_I2C_TARGET=y` — target mode is built in
- `Wire.begin(uint8_t address)` calls Zephyr's `i2c_target_register()`,
  and the `onReceive` / `onRequest` callbacks are implemented
- three I2C controllers are exposed: `i2cs = <&i2c2>, <&i2c4>, <&i2c3>`
  → `Wire`, `Wire1`, `Wire2`
- `i2c3` is on the headers as PC0 = A5, PC1 = A4

### Slave operation, measured 2026-09-29

`claude_test/slave_selftest` proves it with two jumpers, A4↔D20 and
A5↔D21. `Wire2` (i2c3) opens as a `0x21` target and `Wire` (i2c2)
drives the bus as master. **The keypad must be disconnected** for this
— it is also `0x21`.

```text
ADDR 0x21
SCAN 1
WRITE ack=1
READ got=0x5A want=0x5A ok=1
CB req=1 recv=1 last=0xA5
RESULT PASS
```

Address acknowledgement, write ACK, returning a requested byte, and
both callbacks all work. Log:
`claude_test/slave_selftest/unoq_slave_verify.log`.

### Two targets per controller, measured 2026-09-29

The STM32 I2C has two own-address registers, OA1 and OA2, and the
Zephyr driver carries `target_cfg` **and** `target2_cfg` in
`struct i2c_stm32_data`. The second sits behind `CONFIG_I2C_STM32_V2`,
which this build's `autoconf.h` sets to `1`.

Arduino's `Wire` holds one `i2c_target_config` per instance, so it
cannot offer a second address by itself. Bringing the controller up with
`Wire2.begin()` and then calling Zephyr's `i2c_target_register()`
**directly** on the same device does work; `<zephyr/drivers/i2c.h>` and
`DEVICE_DT_GET(DT_NODELABEL(i2c3))` compile from an `.ino` unchanged.

`claude_test/dual_target` reports:

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

Both addresses return their own byte and both callback sets fire
independently. Logs:
`claude_test/dual_target/unoq_dual_target_verify.log` and
`unoq_dual_target_swap_verify.log`, the latter with the roles swapped so
that each of the two controllers is known to hold a pair.

### Controller allocation

```mermaid
flowchart LR
    subgraph UNOQ["Arduino UNO Q · STM32U585"]
        direction LR
        W["<b>Wire = i2c2</b><br/>D20 SDA · D21 SCL<br/>PB11 · PB10"]
        W2["<b>Wire2 = i2c3</b><br/>A4 SDA · A5 SCL<br/>PC1 · PC0"]
        W1["<b>Wire1 = i2c4</b><br/>D42 · D40<br/>high density connector<br/><i>not needed</i>"]
        A1["<b>0x21</b> buttons<br/>own address 1"]
        A2["<b>0x22</b> buttons + 2 LEDs<br/>own address 2"]
        A3["<b>0x60</b> LED dimmer<br/>own address 1"]
        W --> A1
        W --> A2
        W2 --> A3
    end
    BUS["shared SDA / SCL<br/>out through the level shifter"]
    A1 --- BUS
    A2 --- BUS
    A3 --- BUS

    classDef ctrl fill:#d6e6f7,stroke:#2f6ea8,color:#11161d
    classDef addr fill:#d8eedd,stroke:#2f7d55,color:#11161d
    classDef unused fill:#eceff3,stroke:#9aa3b0,color:#3d4a59
    classDef bus fill:#f8e6c0,stroke:#a8761d,color:#11161d
    class W,W2 ctrl
    class W1 unused
    class A1,A2,A3 addr
    class BUS bus
```

Two controllers on the ordinary headers, two addresses each, is four,
and only three are needed. The high density connector stays untouched
and `0x60` is kept — which matters, because the LED writes are the
emulator's state feedback channel.

> Still unmeasured: the tests above had `Wire` acting as **master**. In
> the real emulator both controllers are targets and the mainboard is
> the master. Nothing suggests a problem, but that arrangement itself
> has not been run. A master does not acknowledge its own target
> address, so measuring it needs an external master — which means the
> level shifter and a second board.

### Open items

| Item | Impact |
| --- | --- |
| Mainboard bus voltage | **measured: 5 V.** Level shifter mandatory |
| Three addresses at once from one UNO Q | **resolved.** Target mode and two targets per controller both measured; two header controllers reach four addresses |
| Mainboard polling order, period, init sequence | **measured 2026-10-06.** Six init writes, then `0x21` input registers every 50 ms and nothing else |
| Which `0x22` pins carry the two LEDs | **measured 2026-10-06.** Output port 1, bits 0 and 1, active low |
| Whether INT must be driven | **no.** The 50 ms poll is unconditional, so D2 stays unconnected |
| Minimum key hold time the mainboard accepts | 120 ms works every time; the floor has not been searched |
| Why `LS0` and `LS2` are never written | the down arrow's lamp stayed dark while the key was legal. Either it is driven from elsewhere or that channel assignment needs re-checking |
| Keys beyond the two arrows | not implemented, and refused by name in firmware |
| A physical connector for X1 | not sourced |

The button map is confirmed once. §7 of the specification asks for two
independent reproductions, so a verification pass is still outstanding
for the buttons. The up arrow's lamp, channel 15, now has a second and
independent confirmation: it tracks the Select Process cursor from the
mainboard's own LED writes, with no keypad involved.

## References

- [NXP PCA9555 datasheet](https://www.nxp.com/docs/en/data-sheet/PCA9555.pdf)
- [NXP PCA9532 datasheet](https://www.nxp.com/docs/en/data-sheet/PCA9532.pdf)
- [Arduino UNO Q power specifications](https://docs.arduino.cc/tutorials/uno-q/power-specification/)
- [coport-uni/CommonClaude](https://github.com/coport-uni/CommonClaude)
