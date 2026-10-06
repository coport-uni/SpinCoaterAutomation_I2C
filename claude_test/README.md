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
| `led_only` | Serves the PCA9532 at `0x60` **alone**, from i2c2 (the PCA9306 side), with the A4/A5 jumpers pulled and i2c3 unused, so one controller and one address are on the bus. Records every target callback in RAM and prints nothing for 180 s after boot, then dumps the record. Asks whether `LS2`/`LS0` still lose their data byte without the second controller and the dual-address setup (#18) | Run 2026-10-06, operator present. Runs 1 and 2 (`invalid_run*`) were made with the A4/A5 jumpers still in by mistake and saw **no traffic at all**: with i2c3 never started, its pins on the jumpered wires kept the bus dead, so the main sketch must keep calling `Wire2.begin()`. Run 3 (`run3_0x21_0x60.log`, `run3_lcd.jpg`), jumpers pulled, 0x21 + 0x60 on i2c2 alone: **LS2 and LS0 still lose their data byte and the 0x21 port-0 read still never arrives**, so the second controller is not the cause. Across every log the rule is exact: after an even command byte the follow-up is lost, after an odd one it arrives |
| `ack_timing` | Serves the emulator's three addresses as `pca9555_emu_gui` does, never presses a key, and counts per command byte whether the follow-up arrived (data byte, or a read next) or was lost. Times command byte to `STOP` with the cycle counter, split by parity and outcome, and changes i2c2/i2c3 `TIMINGR` at run time (`TIMING`, `SDADEL`, `SCLDEL`, `PRESC`). `sweep.py` walks `SDADEL` 0..15 and `PRESC` 15 against the mainboard's 50 ms poll and restores the default. Tests the late-ACK hypothesis for #18 and tells it apart from a spurious STOP (#21) | Run 2026-10-06, operator present, no key pressed. Default `TIMINGR` `0x40FC1228` (`SDADEL` 12) on both buses (`boot.log`). `sweep_20261006_202238.log`: `0x21` `[00]` reads delivered 101/101 at `SDADEL` 0..7, 9, 14, 15 and `PRESC` 15; 69/100 at 8, 14/101 at 10, 0/101 at 11..13. `both_sdadel4.log`: `SDADEL` 4 on both buses, 201/201 and 209/209 against 0/201 and 0/209 at the default. `powercycle_sdadel4.log`: spin coater power-cycled with `SDADEL` 4, **zero lost** in four minutes, `LS2` and `LS0` arrive with data, each LS written once instead of about 144 times. Lost transfers stopped 68 µs after the command byte, good ones 124 µs, so the mainboard read a NACK; a spurious STOP is ruled out. One byte takes 300 µs: the bus runs at about 30 kHz |
| `mk2_bench/` | Bench logs of `firmware/pca9555_emu_gui_mk2` (#22). `01_boot.log`: spin coater off, both controllers report `timingr=0x40F41228 sdadel=4 applied=1`. `02_power_on.log`: power-on with no key pressed; the mainboard writes `LS2 = 0x01` and `LS0 = 0x04` with their data bytes, and the panel lines read `LEDS .*......*.....*.` and `LED22 out=0xFC`: tab/pg dn, down arrow, INFO, EDIT MODE and RUN MODE, the five lamps of `KakaoTalk_20261006_142258731.jpg`. `03_keys.log`, `frames/`: `PRESS DOWN 120`, `UP 120`, `PGDN 120`, each held exactly 120 ms. The cursor went to row 2 and back, and `PGDN` turned the list to programs 5 to 8 with lamps `**......*.....**`. The first port-0 key ever delivered | Run 2026-10-06, operator present |

## Host scripts

| Script | Purpose | Status |
| --- | --- | --- |
| `menu_cursor_check/menu_cursor_check.py` | Bench harness for `firmware/pca9555_emu`. Holds one connection to the UNO Q's Monitor link through `adb shell nc 127.0.0.1 7500`, records every line the emulator prints, and grabs a Logitech C920 frame of the spin coater's LCD at each step. `--observe <s>` never writes to the MCU and is what runs while the operator powers the machine on; `--steps DOWN,DOWN,UP` injects keys and photographs the result; `--calibrate` sweeps the lens. Only `UP` and `DOWN` are accepted, and the firmware enforces the same list independently | 2026-10-06 **run on the bench**, both modes |

The virtual keypad started here and has since **moved to
`firmware/pca9555_emu/keypad_gui.py`**, beside the sketch it drives. It
stopped being a probe once it became something the operator uses.

Run it with the `laurell` environment's interpreter, which is the only
Python on this machine with OpenCV and the project's tooling:

```sh
PY="$USERPROFILE/miniconda3/envs/laurell/python.exe"

"$PY" claude_test/menu_cursor_check/menu_cursor_check.py --calibrate
"$PY" claude_test/menu_cursor_check/menu_cursor_check.py --observe 30
"$PY" claude_test/menu_cursor_check/menu_cursor_check.py --steps DOWN,UP
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
| `KakaoTalk_20261006_142258731.jpg` | The real keypad on the Select Process screen, cursor on row 1, 2026-10-06. Five lamps lit: RUN MODE (S18), EDIT MODE (S17), INFO (S15), down arrow (S9), tab/pg dn (S2). The reference the emulator's lamps must match (#18) |
| `lamp_trace/trace_coldboot.log` | `pca9555_emu_gui` with `TRACE ON`, 140 s. **No bus traffic at all**: the spin coater was powered on after the window closed. Kept only as a record of the miss |
| `lamp_trace/trace_running.log`, `lamp_trace/trace_off.log` | `TRACE ON` with the machine running. Only `WREQ`/`CMD`/`STOP 0x22` lines, about 23 write requests a second, no reads, and `STATE` and `TRACE OFF` went unanswered. Printing every callback starves `service_host()`, and the mainboard is left retrying 0x22 |
| `lamp_trace/trace_hush.log` | **The decisive run.** `TRACE ON` then `HUSH 150`, so nothing was printed while the operator powered the machine on. The mainboard writes all four selectors in turn, `LS3`, `LS2`, `LS1`, `LS0`, 1 to 2 ms apart, but `LS2` and `LS0` arrive as a command byte and a `STOP` with no data byte. The lost bytes are a target-side fault, not Monitor load, and not the LED map. The tail is cut off: the 4096-entry ring was still draining when the capture ended |
| `lamp_trace/trace_diag.log` | Second silent capture, `TRACE ON` + `HUSH 90`, now with 0x21 traced, an error callback registered and the drain bounded. **No `ERR` line at all**, so the controllers saw no bus error or lost arbitration. `LS2` and `LS0` lose their data byte exactly as before. The 0x21 trace shows the mainboard's 50 ms poll as `W 0x21 [00]`, `W 0x21 [01]` + 1-byte read, `W 0x22 [00]`: the reads that should follow `[00]` on 0x21 and 0x22 never reach the emulator. The second `TX` of each read is the driver's prefetch, not a byte the mainboard took. The ring overflowed (`drop=5700`) after the part that matters |
| `lamp_trace/press_pgdn.log` | First port-0 key test, `PRESS PGDN 120` with `LOG ON`. **The key stayed down 2.7 s, not 120 ms** (`KEY PGDN down ms=637343`, `up ms=640020`): with every read being logged, one pass of `loop()` took that long and the release waits for it. The operator saw no change on the LCD |
| `lamp_trace/press_pgdn_cam.log`, `lamp_trace/frames/` | Second test with the C920 (camera index 1), `LOG OFF`, 2026-10-06. `PGDN` held 139 ms: frames `00_before` and `01_after_PGDN` are identical. Control: `DOWN` moved the cursor to row 2 and `LS3` to `0x50` (`02_after_DOWN`), `UP` brought it back (`03_after_UP`). Port-1 keys work; the one port-0 key tried did nothing. The operator expects the arrows, not PGDN, to move this menu, so this is consistent with the lost port-0 read but does not prove it |
| `key_release/bench.log`, `key_release/*.jpg` | `pca9555_emu_gui` with the release moved to a `k_timer` (#19), 2026-10-06, operator present. `PRESS DOWN 120` and `PRESS UP 120` with `LOG ON`: `KEY DOWN down ms=69764` / `up ms=69884` and `KEY UP down ms=102538` / `up ms=102658`, both exactly 120 ms, although `loop()` itself ran about 2.7 s late (the `LEDS` lines). A `STATE` caught mid-way shows `a1=0xFF` with `press=UP`: the bit was back on time and only the report was pending. The LCD moved one row down and back up (`01_after_DOWN`, `02_after_UP`) |

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
