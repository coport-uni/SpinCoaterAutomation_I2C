# pca9555_emu_gui — the whole panel, gated by the lamps

> **Run on the bench 2026-10-06**, operator present. What was actually
> exercised: the three addresses registering, the mainboard booting
> against it, `KEYS`, `HELP`, both rejection paths, and the two arrow
> keys moving the cursor on camera. Later the same day: the `LEDS` and
> `LED22` lamp lines, the panel layout, and the timer-driven key
> release (both arrows released at 120 ms with `loop()` 2.7 s late).
>
> **Not exercised: the other 16 keys.** `EDIT` and `RUN` matter most of
> those, because they are the only ones on expander `0x22` and so the
> only ones that use the second device in `press_dev`. The 14 remaining
> `0x21` keys share their whole code path with the arrows. Nothing that
> can turn the chuck has been pressed.
>
> **Known fault (#18), fixed in
> [`../pca9555_emu_gui_mk2/`](../pca9555_emu_gui_mk2/README.md):**
> every transfer after an even command byte is lost here, so
> `LS0`/`LS2` and the port-0 reads never arrive. This sketch is kept
> unchanged as the record of the first full-panel build; use mk2.

Same structure as its sibling, with two differences:

| | `../pca9555_emu/` | this folder |
| --- | --- | --- |
| Keys the sketch accepts | `UP` and `DOWN` only | all 18 |
| Keys the panel lets you click | `UP` and `DOWN` only | **whichever lamps are lit** |
| Verified | yes, 2026-10-06 | arrows and the command surface only |

## The idea

The mainboard lights a key's lamp while that key is a legal input in the
current machine state. So instead of a list of permitted keys written
into the GUI, the window follows the machine: a key is clickable exactly
while its lamp is lit. The spin coater decides, not this file.

```mermaid
flowchart LR
    MB["<b>Mainboard</b><br/>decides what is legal"]
    W["writes LS0..LS3<br/>and 0x22 port 1"]
    EMU["<b>pca9555_emu_gui.ino</b><br/>decodes to LEDS and LED22 lines"]
    GUI["<b>keypad_gui.py</b><br/>lamp lit → key enabled"]
    OP["Operator<br/>can only click what is legal"]

    MB --> W --> EMU --> GUI --> OP
    OP -.->|"PRESS &lt;key&gt;"| EMU
    EMU -.->|"bit held low"| MB

    classDef ext fill:#eceff3,stroke:#9aa3b0,color:#11161d
    classDef dev fill:#d8eedd,stroke:#2f7d55,color:#11161d
    classDef pc fill:#dfe8f5,stroke:#4571a8,color:#11161d
    class MB,W ext
    class EMU dev
    class GUI,OP pc
```

This is **not** a safety interlock. The lid, the machine's own logic and
the mains switch are what stand between a key press and harm, exactly as
they did when the real keypad was plugged in.

## Why there is an "unlock all keys" switch

The gate is incomplete, and the switch exists because of it.

On a screen change the mainboard writes all four PCA9532 selectors,
`LS3`, `LS2`, `LS1`, `LS0`. Only `LS3` and `LS1` arrive with their data
byte; the transfers to `LS2` and `LS0` reach the emulator as a command
byte and a STOP. Channels 0–3 and 8–11 therefore stay at the power-on
default, off, and **the down arrow is one of them**, so a strictly
lamp-gated panel cannot walk down a menu at all. The real keypad on the
same Select Process screen lights the down arrow and tab/pg dn as well.

What did arrive, from a cold boot, 2026-10-06:

| Cursor row | `LS3` | Lit on the PCA9532 |
| --- | --- | --- |
| 1 | `0x10` | channel 14, INFO |
| 2–4 | `0x50` | channels 14 and 15, INFO and up arrow |

The loss follows the command byte, not the register: every follow-up
after an even command byte is lost, including the port-0 reads of
`0x21` and `0x22`. See "The open fault" in the top-level README and
[#18](https://github.com/coport-uni/SpinCoaterAutomation_I2C/issues/18).

Until that is fixed, the switch turns the gate off and lets every key
through. Leaving it on is the honest default; ticking it is a deliberate
act. Note that the port-0 keys (`PGUP PGDN FWD RIGHT F2 F1 VACUUM
SELECT`, and `EDIT`/`RUN` on `0x22`) probably do nothing yet, because
the mainboard's port-0 read is one of the lost transfers; `PGDN` was
pressed once and changed nothing on the LCD.

## Running it, in order

Identical to `../pca9555_emu/README.md`, with this folder's names:

```sh
CLI="/c/Program Files/Arduino IDE/resources/app/lib/backend/resources/arduino-cli.exe"

# 1. spin coater OFF
"$CLI" compile --fqbn arduino:zephyr:unoq firmware/pca9555_emu_gui
"$CLI" upload  --fqbn arduino:zephyr:unoq -p COM17 firmware/pca9555_emu_gui

# 2. watch for BOOT / REG rc=0 x3 / READY before going on
ADB="$LOCALAPPDATA/Arduino15/packages/arduino/tools/adb/32.0.0/adb.exe"
"$ADB" shell "nc 127.0.0.1 7500"

# 3. spin coater ON, confirm the LCD comes up and no key reads pressed

# 4. open the panel
"$USERPROFILE/miniconda3/envs/laurell/python.exe" \
    firmware/pca9555_emu_gui/keypad_gui.py
```

See `../pca9555_emu/README.md` for how the Tk side is put together; the
threading, the queue and the `after()` drain are the same here.

## Host protocol

```
PRESS <key> [ms]   hold one key, default 100 ms, range 20..1000
RELEASE            release early
KEYS               list every key name
STATE              print the register files and the INT level
LOG ON | LOG OFF   log every read, not only the ones that changed
TRACE ON | OFF     log every callback on 0x22 and 0x60, raw
HUSH <s>           print nothing for s seconds (1..600), then everything
HELP               list the commands
```

`TRACE ON` adds `WREQ`, `CMD`, `RREQ` and `STOP` lines for 0x22 and
0x60, so the log shows each command byte and each transfer that carries
no data, which the `RX`/`TX` lines alone cannot. It exists to find the
`LS0` and `LS2` data bytes that never arrive (#18). It is off at boot.

Use it with `HUSH`. Printed live, the trace keeps the sketch too busy to
answer host commands, and the mainboard is left retrying 0x22. While
hushed nothing is printed, 0x21 is traced as well, and the 8192-entry
ring holds everything until the window ends:

```
TRACE ON
HUSH 90        # then power the spin coater on
```

`ERR <addr> code=<n>` lines appear whether or not the trace is on. They
report a bus error (`code=4`) or lost arbitration (`code=1`) seen by the
controller, under the first address registered on it.

Key names: `PGUP PGDN FWD RIGHT F2 F1 VACUUM SELECT DOWN REV LEFT PAUSE
STOP START INFO UP EDIT RUN`.

### Lamp lines

The panel reads every lamp from two lines. The sketch prints each one
when the mainboard writes a register behind it, and `STATE` prints both,
so a panel opened after the mainboard has set its lamps still sees them.

```
LEDS ..............** psc0=0xFF pwm0=0x00 psc1=0xFF pwm1=0x80 ms=190459
LED22 out=0xFC cfg=0x00 ms=190470
```

| Line | Source | Lamp is lit when |
| --- | --- | --- |
| `LEDS` | PCA9532 `LS0`..`LS3`, `PSC0`..`PWM1` | `*`, or `0`/`1` with that PWM's duty register non-zero |
| `LED22` | 0x22 port 1, output and configuration | the bit is an output (`cfg` 0) driven low (`out` 0) |

A blink output is on for `PWM/256` of its period, so a duty register of
`0x00` keeps a channel dark. The mainboard writes `PWM0 = 0x00` at boot.
EDIT MODE is bit 0 and RUN MODE bit 1 of `LED22`.

## Before the first run

`START`, `FWD`, `REV` and `VACUUM` can set the chuck turning or pull
vacuum. With the real keypad unplugged there is no physical STOP button,
so **the mains switch is the stop and belongs within reach.** Chuck
empty, lid closed, operator at the bench.
