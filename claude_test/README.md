# claude_test

One-off probe sketches and the bench logs they produced. Deliverable
firmware lives in `firmware/`.

## Sketches

| Sketch | Purpose | Status |
| --- | --- | --- |
| `keypad_probe` | First hardware response check on the keypad board. Reads the PCA9555 configuration registers `0x06` and `0x07`, polls the input registers `0x00` and `0x01` every 50 ms, and reports D2 INT edges. The second pass widened the watch to 48 bits by including the PCA9532 input registers | Run on the UNO Q, 2026-09-29. Three I2C slaves answered, key presses held their level, and every D2 INT edge appeared |
| `bus_check` | Reads the idle levels of SDA, SCL and INT **without touching `Wire` at all**. Sampling under two conditions, high impedance and internal pull-up, separates **external pull-up present / floating / something holding it low** | Run on the R4 Minima, 2026-09-29. `SCL hiz=0 pup=1` pinned down the missing SCL pull-up (issue #3) |
| `slave_selftest` | Proves the UNO Q works as an I2C target **using one board**. `Wire2` (i2c3, A4/A5) opens as a `0x21` target while `Wire` (i2c2, D20/D21) scans, writes and reads as master. Needs two jumpers, A4↔D20 and A5↔D21, and **the keypad must be disconnected** — it is also `0x21` | 2026-09-29 **PASS**. Address acknowledgement, write ACK, byte return, and both callbacks all confirmed |
| `dual_target` | Tests whether **one controller can hold two addresses**. The STM32 I2C has two own-address registers, OA1 and OA2, and the Zephyr driver carries `target_cfg` / `target2_cfg` (`CONFIG_I2C_STM32_V2=1`). Arduino's `Wire` allows one target per instance, so the second address is registered by calling `i2c_target_register()` directly. `TARGETS_ON_WIRE` selects which controller carries the pair — a master does not acknowledge its own target address, so a controller cannot verify itself and each role has to be run in turn | 2026-09-29 **PASS in both roles**. Verified with `TARGETS_ON_WIRE 0` (targets on i2c3) and with `1` (targets on i2c2) |

## Host scripts

| Script | Purpose | Status |
| --- | --- | --- |
| `menu_cursor_check/menu_cursor_check.py` | Bench harness for `firmware/pca9555_emu`. Holds one connection to the UNO Q's Monitor link through `adb shell nc 127.0.0.1 7500`, records every line the emulator prints, and grabs a Logitech C920 frame of the spin coater's LCD at each step. `--observe <s>` never writes to the MCU and is what runs while the operator powers the machine on; `--steps DOWN,DOWN,UP` injects keys and photographs the result; `--calibrate` sweeps the lens. Only `UP` and `DOWN` are accepted, and the firmware enforces the same list independently | 2026-10-06 **run on the bench**, both modes |
| `keypad_gui/keypad_gui.py` | A virtual Laurell keypad on the PC. 18 buttons with their indicator lamps, lit from the LED traffic the mainboard is sending right now: `LEDS` lines for the PCA9532 channels and `RX 0x22 reg=0x03` writes for the EDIT MODE and RUN MODE lamps. Because the mainboard lights a key only while it is a legal input, the window is a live list of the commands the machine will accept. Only the arrows are clickable; every other key is drawn greyed and refused by the firmware as well | 2026-10-06, map and parser checked against real log lines; window opened against the live bus |

Run both with the `laurell` environment's interpreter, which is the only
Python on this machine with OpenCV and the project's tooling:

```sh
PY="$USERPROFILE/miniconda3/envs/laurell/python.exe"

"$PY" claude_test/menu_cursor_check/menu_cursor_check.py --observe 30
"$PY" claude_test/menu_cursor_check/menu_cursor_check.py --steps DOWN,UP
"$PY" claude_test/keypad_gui/keypad_gui.py
```

Frames and logs land in `menu_cursor_check/runs/<timestamp>/`.

### Two things that cost time here

The C920's autofocus hunts on the LCD's flat backlit face and settles
soft. `--calibrate` sweeps `CAP_PROP_FOCUS` and scores each step by the
Laplacian variance of the LCD region; 165 measured about 20 % sharper
than anything autofocus chose. Focus is pinned to that value.

`cv2.imwrite` goes through the C runtime's narrow-char file API and
**silently returns false** on a path containing non-ASCII characters,
which this repository's own directory has. Frames are encoded in memory
with `cv2.imencode` and written from Python instead.

## Bench logs

| Log | Contents |
| --- | --- |
| `keypad_probe/unoq_scan_verify.log` | Bus scan on the UNO Q with the keypad attached |
| `keypad_probe/unoq_led_verify.log` | PCA9532 LED control confirmed on the UNO Q |
| `keypad_probe/button_map_pass1.log` | First button mapping pass. All 18 keys pressed in order, 17 detected, 36 `CHG` lines. Only VACUUM went undetected, and the 6,527 ms gap between key 13 and key 14 — a whole cycle missing — identified that slot as VACUUM. The basis for `docs/button_map.json` |
| `keypad_probe/button_map_pass2_vacuum.log` | Second pass. With the watch widened to the PCA9532 input registers, VACUUM was pressed three times and detected once, fixing it at `0x21` register `0x00` bit 6. The dome switch's poor contact is recorded in this log too |
| `keypad_probe/r4_minima_led_check.log` | An attempt to run `firmware/pca9532_led` with the keypad on 5 V from an UNO R4 Minima. **This is a record of failure, not evidence of success.** The commands arrived intact but every I2C write was NACKed |
| `slave_selftest/unoq_slave_verify.log` | UNO Q target mode, PASS |
| `dual_target/unoq_dual_target_verify.log` | Two targets on i2c3, i2c2 as master, PASS |
| `dual_target/unoq_dual_target_swap_verify.log` | Roles swapped: two targets on i2c2, i2c3 as master, PASS |
| `menu_cursor_check/runs/20261006_112031/` | **The first run against the spin coater mainboard.** All three addresses registered `rc=0`, then the operator powered the machine on. Holds the six init writes, the 50 ms polling of `0x21`, and 95 one-second summaries with `drop=0`. The LCD came up normally and no key was ever read as pressed |
| `menu_cursor_check/runs/20261006_112326/` | First key injection: DOWN ×3 then UP ×3, seven frames. The cursor walks 1→2→3→4→3→2→1. Frames are soft; this run is what prompted the focus work |
| `menu_cursor_check/runs/20261006_113356/` | Same sequence repeated with the lens pinned at focus 165. Sharp enough to read the row numbers directly, and an independent second reproduction of the first injection run |

## Reading the serial output differs per board

### UNO Q

The MCU's `Serial` does not reach the host directly. The Router Bridge
`Monitor` object goes out to `127.0.0.1:7500` on the Linux side, and
`arduino-router-serial.service` relays that to `/dev/ttyGS0`. When the
host COM port reads nothing, read the socket directly over adb.

```sh
ADB="$LOCALAPPDATA/Arduino15/packages/arduino/tools/adb/32.0.0/adb.exe"
"$ADB" shell "nc 127.0.0.1 7500"
```

`setup()` output appears once, right after boot, so start the capture
first and re-upload to force a reset or the initial register dump is
lost.

### UNO R4 Minima

An ordinary USB CDC, readable straight from the COM port. But
**opening the port asserts DTR and resets the board.** A command sent
immediately is swallowed by the boot banner and comes back as `ERR`, so
wait about 2.5 s after opening before sending anything.

## Build and upload

```sh
CLI="/c/Program Files/Arduino IDE/resources/app/lib/backend/resources/arduino-cli.exe"

# UNO Q
"$CLI" compile --fqbn arduino:zephyr:unoq         claude_test/keypad_probe
"$CLI" upload  --fqbn arduino:zephyr:unoq -p COM17 claude_test/keypad_probe

# UNO R4 Minima
"$CLI" compile --fqbn arduino:renesas_uno:minima         claude_test/bus_check
"$CLI" upload  --fqbn arduino:renesas_uno:minima -p COM18 claude_test/bus_check
```

The R4 often stays in DFU mode after an upload instead of coming back
as a COM port. Re-upload with the DFU port named explicitly, such as
`-p 1-9`. It happens most with sketches that use `Wire`, which suggests
the Renesas `Wire` stalls during start-up when the bus is abnormal.
