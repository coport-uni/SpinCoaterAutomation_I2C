# ToDo

Append-only task log for this repository, per `CLAUDE.md` §4 Task
Management. Never delete or reorder entries; only flip `- [ ]` to `- [x]`
and append commit hashes or issue links to completed lines.

---

## 1. Adopt the CommonClaude harness (2026-09-29)

Add `coport-uni/CommonClaude` as a submodule under `external/` and wire
its conventions and enforcement hooks into this repository.

- [x] Add the submodule at `external/CommonClaude`
- [x] Add `.claude/settings.json` pointing the hooks at the submodule
- [x] Add a project `CLAUDE.md` that imports the shared ruleset and
      records the C++/Arduino, language, and linting overrides
- [x] Create this `ToDo.md`
- [x] Register the GitHub issue via `gh issue create` (#1)
- [x] Open the PR via `gh pr create`

Notes: no hardware verification applies — this change touches no sketch.
Verification is that each hook runs and that the settings file parses.

---

## 2. Stage 1 exploration: map every button and LED (2026-09-29)

Bring up the keypad board standalone on an Arduino UNO Q at 3.3 V and
map all 18 buttons and all LEDs, per `docs/spincoater_keypad_spec.md`
§1 stage 1.

- [x] Install `arduino:zephyr@1.0.0` and `Arduino_RouterBridge`
- [x] Write `firmware/i2c_scan` and confirm three slaves on real
      hardware: `0x21`, `0x22`, `0x60`
- [x] Read the PCA9555 configuration registers to confirm chip identity
- [x] Write `claude_test/keypad_probe` and map all 18 buttons
- [x] Confirm header X1 pin 5 is INT, not RESET
- [x] Write `firmware/pca9532_led` and map all 16 PCA9532 LED channels
- [x] Record `docs/button_map.json` and `docs/led_map.json`
- [x] Write `README.md`
- [x] Register the GitHub issue via `gh issue create` (#10)
- [ ] Second reproduction pass; the spec §7 asks for two, every entry is
      currently confirmed once

Verification: run on the real board with the operator present. Console
output kept in `claude_test/keypad_probe/button_map_pass1.log` and
`button_map_pass2_vacuum.log`. Committed as `ae34474` and pushed to
`main` at the user's explicit instruction, without a working branch or
PR; §4 steps 5 and 10 were not followed for that commit.

---

## 3. Build the LED sketch for the UNO R4 Minima too (2026-09-29)

The mainboard I2C bus measured 5 V, so the 3.3 V UNO Q cannot be wired
to it. Move the bench work to an UNO R4 Minima, which is 5 V native.
`firmware/pca9532_led` used the UNO Q `Monitor` object, which does not
exist on other boards.

- [x] Add a `HOST` alias selected by `ARDUINO_ARCH_ZEPHYR` so the same
      sketch builds for both boards
- [x] Compile for `arduino:renesas_uno:minima` — 18% flash
- [x] Compile for `arduino:zephyr:unoq` — 11% flash, no regression
- [x] Upload to the R4 Minima on COM18
- [ ] **BLOCKED**: the R4 stayed in DFU after upload and never
      re-enumerated as a serial port. Waiting on the operator to press
      RESET
- [ ] Confirm the LEDs light on real hardware and keep the console output
- [x] Register the GitHub issue via `gh issue create` (#10)
- [ ] Commit — held back by §5.1, the change is not verified yet

Wiring changes from the UNO Q: keypad VDD moves from 3.3 V to 5 V, SDA
moves from D20 to A4, SCL moves from D21 to A5. INT stays on D2.

---

## 4. Keypad emulator for the mainboard (2026-09-29)

Write `firmware/pca9555_emu`, the slave emulator that replaces the
keypad board so the host can inject key presses, per spec §1 stage 3.

- [x] First draft targeting ATmega328P, raw TWI with `TWAR`/`TWAMR`
      address masking to answer `0x21`, `0x22` and `0x60` from one
      peripheral
- [x] Compiles for `arduino:avr:uno` — 19% flash, 50% RAM
- [ ] **Not run on hardware. Not committed**, per §5.1
- [ ] **Rework needed**: the operator has an UNO R4 Minima, not an AVR.
      The raw TWI code will not compile for Renesas RA4M1, and the
      Minima exposes only one I2C bus (`WIRE_HOWMANY 1`), so stock Wire
      gives one slave address where three are needed
- [ ] Decide the approach: drive the RA4M1 IIC registers directly, try
      `0x21` alone and see whether the mainboard tolerates NAKs on the
      other two, or use several boards
- [ ] Measure where the I2C pull-ups live — keypad board or mainboard.
      The current code assumes the mainboard supplies them
- [x] Register the GitHub issue via `gh issue create` (#10)

Safety: injecting keys can spin the chuck. First tests use a harmless
key such as INFO, with nothing mounted and the lid closed.

---

## 5. Apply the CommonClaude C-language convention (2026-09-29)

The operator noted that CommonClaude has a C-language branch. Fold its
C-specific rules into the project overrides in `CLAUDE.md`.

- [x] Enumerate the submodule's remote branches:
      `feat/c-language-support` and `fix/windows-hook-paths`
- [x] Determine that both are ancestors of `main` with 0 unique commits,
      so there is nothing to merge
- [x] Find that `e4f6e8d refactor(claude-md): replace C-language style
      with Python convention` reverted the C rules on `main`, so the C
      content lives only at `c18da89` and is absent from `main`
- [x] Decide to keep the submodule pinned to `main` rather than move it
      to the C branch — the branch is 19 commits behind and lacks the
      §5.1 Verification Gate, the MCP/`gh` rules, and the Windows
      hook-path fix this machine needs
- [x] Port the C sections of `c18da89` into `CLAUDE.md` §3: naming
      table, Doxygen in place of PEP 257, the C block TODO format, and
      the `claude_test/` Doxygen waiver
- [x] Port the C linting rule into `CLAUDE.md` §4: clang-format and
      cppcheck in place of Ruff, with the exact commands
- [x] Confirm `.clang-format` is byte-identical on `main` and `c18da89`,
      so no file needs copying
- [x] Register the GitHub issue via `gh issue create` (#2)
- [x] Cross-reference the follow-up on the harness issue (#1)
- [x] Open the PR via `gh pr create` (#11)

Scope: the operator asked for the C-language content only, so the C
branch's §13 `.gitignore` template and its §16 pre-commit
`mirrors-clang-format` config were deliberately left out.

Not applied: `clang-format`, `cppcheck` and `pre-commit` are all absent
from this machine, so the C lint commands are documented but cannot run
yet. `arduino-cli compile` remains the enforced pre-commit check.

---

## 5. Diagnose the dead I2C bus on the UNO R4 Minima (2026-09-29)

Every sketch that called `Wire` on the R4 failed: the board stayed in
DFU and never enumerated as a CDC port, and on the runs where it did
come up, every transfer was answered with a NACK.

- [x] Prove the fault is not in the command path — instrument
      `firmware/pca9532_led` to echo the received line. It answered
      `ERR raw=[ALL off]`, so the line arrived intact and the failure
      was the I2C write underneath
- [x] Establish that `setup()` reaching `READY` proves nothing, because
      it ignored the return value of every `pca9532_write`
- [x] Write `claude_test/bus_check`, which never calls `Wire`, to sample
      SDA, SCL and INT as plain GPIO in both high impedance and internal
      pull-up. It boots reliably, which by itself isolates the fault to
      `Wire`
- [x] Measure the bus: `SDA hiz=1 pup=1`, `SCL hiz=0 pup=1`,
      `INT hiz=1 pup=1`. **SCL floats; SDA and INT have external
      pull-ups**
- [x] Add `bus_check()` and a `HOST` alias to `firmware/i2c_scan` so it
      builds and reports on both boards
- [x] Try the internal pull-ups on SDA and SCL — the board now boots and
      `Wire.begin()` no longer hangs, but `SCAN done 0`
- [x] Try dropping the bus to 10 kHz for the slow rise time — still
      `SCAN done 0`
- [ ] **BLOCKED on a bench measurement.** Software has gone as far as it
      can. A floating SCL plus zero acknowledgements points at the SCL
      wire not reaching the keypad, not merely at a missing resistor
- [ ] Reseat the SCL wire between R4 `A5` and X1 pin 4, and confirm
      keypad VDD is on the R4 `5V` pin with a common ground
- [ ] Measure X1 pin 4 to PCA9555 pin 22; spec §3 step 1 expects 900 to
      1100 ohm if R16 is in series
- [ ] Fit 4.7 kOhm from SCL to 5 V and restore the bus to 100 kHz
- [x] Register the GitHub issue via `gh issue create` (#3)
- [ ] Commit — held back by §5.1, nothing here is verified working yet

Finding worth keeping: the keypad board pulls SDA and INT up but not
SCL, so the mainboard supplied the SCL pull-up in the original machine.
The UNO Q hid this because its Zephyr pinctrl biases the I2C pins up;
the Renesas core does not.

---

## 6. Return to the UNO Q and add a level shifter (2026-09-29)

The R4 Minima bench bus stayed dead (section 5), and the operator chose
to go back to the UNO Q and put a level shifter between it and the 5 V
mainboard instead. Three things make that the stronger option:

- The UNO Q bench setup is **proven**. All of stage 1 was mapped on it.
- Its Zephyr pinctrl biases the I2C pins up, which is exactly why the
  missing SCL pull-up never bit there.
- It exposes **three** I2C controllers, `i2cs = <&i2c2>, <&i2c4>,
  <&i2c3>`, with `CONFIG_I2C_TARGET=y`. The R4 Minima exposes one
  (`WIRE_HOWMANY 1`), so it can never answer the three slave addresses
  the emulator needs.

Verification on the UNO Q, operator present, console output kept:

- [x] `firmware/pca9532_led` — `BOOT` / `I2C up` / `READY`, then
      `LED 0 on` -> `OK` with the pg up LED confirmed lit by the
      operator, `LED 13 on` -> `OK`, `ALL on` -> `ERR` as designed,
      `ALL off` -> `OK`. Log in
      `claude_test/keypad_probe/unoq_led_verify.log`
- [x] `firmware/i2c_scan` — `ADDR 0x21`, `ADDR 0x22`, `ADDR 0x60`,
      `SCAN done 3`. Log in
      `claude_test/keypad_probe/unoq_scan_verify.log`
- [x] Guard the R4-only pull-up and 10 kHz workaround behind
      `ARDUINO_ARCH_ZEPHYR` so the UNO Q path is untouched by it
- [ ] Source a BSS138 style level shifter and wire SDA, SCL and INT
      through it, LV to 3.3 V and HV to the mainboard 5 V
- [x] Prove UNO Q slave mode with the A4/A5 to D20/D21 jumper self-test,
      keypad disconnected to avoid the `0x21` address clash (PR #7)
- [x] Rewrite `firmware/pca9555_emu` for the UNO Q; the current draft is
      raw AVR TWI and does not apply (PR #13)
- [x] Register the GitHub issue via `gh issue create` (#4)

`firmware/pca9555_emu` stays out of this commit: it has never run on any
board, per section 5.1.

---

## 7. Prove UNO Q slave mode with a self-test (2026-09-29)

Spec section 8 listed "UNO Q Wire slave support unverified" as an open
risk, and the whole stage 3 board choice hung on it. Settle it with two
jumper wires and no extra hardware.

- [x] Delete the ATmega328P draft of `firmware/pca9555_emu`. It never
      ran on any board and its raw AVR TWI layer does not apply to the
      UNO Q, so it was removed rather than carried
- [x] Write `claude_test/slave_selftest`: `Wire2` (i2c3, A4/A5) opened
      as a target at `0x21`, `Wire` (i2c2, D20/D21) driving the bus as
      a master, jumpers A4 to D20 and A5 to D21
- [x] **PASS on real hardware, operator present, keypad disconnected.**
      `SCAN 1` with only `ADDR 0x21`, `WRITE ack=1`,
      `READ got=0x5A want=0x5A ok=1`, `CB req=1 recv=1 last=0xA5`. Log
      in `claude_test/slave_selftest/unoq_slave_verify.log`
- [x] Establish where the third controller lives: `i2c4` maps to PF15
      and PF14, which are D42 and D40 on the high density connector,
      not the ordinary headers
- [x] Decide how to serve three addresses. Either break `i2c4` out of
      the high density connector, or find out whether the Zephyr STM32
      driver can register two targets on one controller using OA1 and
      OA2, or drop `0x60` and check that the mainboard tolerates a NAK
      — settled: OA1 plus OA2 on i2c2 carries `0x21` and `0x22`, i2c3
      carries `0x60`, and `i2c4` stays unused (PR #8, #9, #13)
      there
- [x] Register the GitHub issue via `gh issue create` (#4)

This closes the largest unknown in the spec's risk table. The UNO Q can
answer as an I2C slave at the address the emulator needs, with both
`onReceive` and `onRequest` firing, so the board choice for stage 3 is
settled and no Nano is required.

---

## 8. Answer two addresses from one I2C controller (2026-09-29)

Section 7 proved the UNO Q can be a target, but only two of its three
controllers reach the ordinary headers while the emulator needs three
addresses. Rather than break i2c4 out of the high density connector or
give up `0x60`, check whether one controller can hold two addresses.

- [x] Read the driver: `struct i2c_stm32_data` carries `target_cfg`
      **and** `target2_cfg`, mirroring the STM32 OA1 and OA2 own-address
      registers. The second is behind `CONFIG_I2C_STM32_V2`, which the
      generated `autoconf.h` sets to 1 for this board
- [x] Confirm a sketch can reach the Zephyr driver API directly:
      `<zephyr/drivers/i2c.h>` and `DEVICE_DT_GET(DT_NODELABEL(i2c3))`
      both compile from an `.ino`
- [x] Write `claude_test/dual_target`: `0x21` registered through the
      Arduino `Wire` API, `0x22` registered by calling
      `i2c_target_register()` on the same device
- [x] **PASS on real hardware, operator present, keypad disconnected.**
      `REGISTER2 rc=0`, `SCAN 2` with both `ADDR 0x21` and `ADDR 0x22`,
      `READ1 got=0x5A`, `READ2 got=0xB7`, and both callback sets firing
      independently. Log in
      `claude_test/dual_target/unoq_dual_target_verify.log`
- [x] Register the GitHub issue via `gh issue create` (#4)

Two controllers times two addresses is four, and three are needed, so
the address problem is settled. The high density connector stays
unused and `0x60` is kept, which matters because the LED writes are the
emulator's state feedback channel.

Not yet verified: the test had `Wire` acting as master. In the real
emulator both controllers are targets and the mainboard is the master.

---

## 9. Verify the second controller holds two targets as well (2026-09-29)

Section 8 measured a pair of targets on i2c3 while i2c2 drove the bus.
Calling that "two controllers times two addresses is four" was
arithmetic, not a measurement: i2c2 had never been a target at all. The
operator caught this.

- [x] Add a `TARGETS_ON_WIRE` switch to `claude_test/dual_target` so
      either controller can carry the pair while the other drives
- [x] **PASS with `TARGETS_ON_WIRE 1`**, targets on i2c2 and i2c3 as
      master, operator present, keypad disconnected. `REGISTER2 rc=0`,
      `SCAN 2` with `ADDR 0x21` and `ADDR 0x22`, `READ1 got=0x5A`,
      `READ2 got=0xB7`, both callback sets firing. Log in
      `claude_test/dual_target/unoq_dual_target_swap_verify.log`
- [x] Three or four addresses answering **at once** is still unmeasured.
      A master does not acknowledge its own target address, so with only
      two controllers one of them must be the master and at most two
      targets are ever observable. This needs an external master, which
      means the level shifter and the R4 Minima, or a bus the Linux side
      can reach
      — measured 2026-10-06. The external master turned out to be the
      spin coater itself. With `firmware/pca9555_emu` on the UNO Q, the
      mainboard addressed `0x21`, `0x22` and `0x60` on one bus and all
      three answered in the same session: two targets on i2c2 via OA1
      and OA2, one on i2c3. Evidence:
      `claude_test/menu_cursor_check/runs/20261006_112031/monitor.log`
      (PR #13)

Both controllers are now known to hold a pair each, so the pair can sit
on whichever one suits the wiring. Note the emulator needs three
addresses, not four: 0x21 and 0x22 on one controller, 0x60 on the
other. That arrangement was already covered by section 8.

---

## 10. Rewrite the README in English and draw the concept (2026-09-30)

The operator asked for `README.md` in English and for the project's
concept to be carried by pictures, not prose alone. Project `CLAUDE.md`
§2 currently overrides the shared ruleset to keep Korean prose
documentation, which contradicts CommonClaude §2 Language —
"documentation files (including README) ... must be written in
**English**". The operator's decision is to drop that override and align
with the shared rule, so every document in this repository becomes
English.

- [x] Replace project `CLAUDE.md` §2 with an English-documentation rule
      that matches CommonClaude §2, and record the diagram convention
      (Mermaid inline, hand-written SVG under `docs/diagrams/`)
- [x] Add the SVG diagrams: keypad button and bit layout, level shifter
      wiring to the mainboard. Three files, the register and bit
      layout having earned a figure of its own:
      `docs/diagrams/keypad_bitmap.svg`, `register_map.svg`,
      `level_shifter.svg`
- [x] Rewrite `README.md` in English with Mermaid diagrams — the
      three-stage roadmap, the signal path before and after the
      emulator, and the UNO Q controller-to-address allocation —
      referencing the SVGs
- [x] Translate the remaining Korean prose documents so the new rule
      holds repository-wide: `docs/spincoater_keypad_spec.md` and
      `claude_test/README.md`
- [x] Verify every factual claim in the new README against
      `docs/button_map.json`, `docs/led_map.json`, the `claude_test`
      logs, and the sketches. A checker walked both SVGs against the
      two JSON maps: 74 assertions, all passing, no Korean left in any
      tracked file
- [x] Confirm the Mermaid blocks parse and the SVGs render. All five
      Mermaid blocks parse and render under mermaid 11; the three SVGs
      are well-formed and were inspected as PNG renders
- [x] Register the GitHub issue via `gh issue create` (#10)
- [x] Work on branch `docs/english-readme-with-diagrams`
- [x] Open the PR via `gh pr create` (#11)

No sketch changes, so §5.1 requires no hardware run for this entry. The
applicable row is documentation: every claim checked against the file,
log, or measurement it describes. The submodule at
`external/CommonClaude` was empty in this working copy and was restored
with `git submodule update --init --recursive`; the recorded commit is
unchanged, so nothing is staged for it.

---

## 11. Move the menu cursor through the emulator, check on camera (2026-10-06)

The operator has wired the UNO Q to the mainboard through a SparkFun
PCA9306 translator (D20 SDA, D21 SCL, jumpers A4 to D20 and A5 to D21)
and unplugged the keypad board. A Logitech C920 watches the LCD.
Goal: inject the down and up arrow keys over I2C, move the "Select
Process" cursor 1 -> 2 -> 3 -> 4 -> 3 -> 2 -> 1, and confirm every step
on the camera. That proves the emulator talks to the mainboard.

Findings that shape the design:

- Arduino `Wire` calls `onReceive` only at STOP, so for a register
  write followed by a repeated-start read, `onRequest` fires before the
  register pointer arrives. All three addresses are therefore served by
  raw Zephyr `i2c_target_callbacks`, as `claude_test/dual_target` did
  for its second address.
- The sketch last flashed may be `claude_test/dual_target`, which
  answers `0x21` with `0x5A` (START, up, down and left read as pressed)
  and drives the bus as a master every 3 s. Which sketch is on the MCU
  cannot be read back, so the first flash happens with the spin coater
  powered off.
- With the keypad unplugged there is no physical STOP key. The mains
  switch is the stop for every run.
- The PCA9306 carries two channels and both are taken by SDA and SCL,
  so INT has none left. Wiring it straight across would put the
  mainboard's 5 V pull-up on D2 whenever the line is released, which
  the UNO Q does not tolerate, so INT stays unconnected and `INT_WIRED`
  is 0. Whether the mainboard polls without it is what bench run 1
  answers.
- The keypad board carried the SDA pull-up and the mainboard carried
  SCL's, which is what `SCL hiz=0 pup=1` showed on the R4 in #3. With
  the keypad gone the 5 V side needs the PCA9306 breakout's own
  pull-ups enabled on both sides; the part is a pass gate and drives
  neither side high by itself.
- Smart App Control is enforced on this PC
  (`VerifiedAndReputablePolicyState = 1`) and blocks the unsigned
  `cc1.exe` of the Zephyr toolchain, so `arduino-cli compile` dies with
  `CreateProcess: No such file or directory`. The compile step is
  blocked until that is resolved.

Tasks:

- [x] Register the GitHub issue via `gh issue create` (#12)
- [x] Cut `feat/pca9555-emu` from `main`
- [x] Write `firmware/pca9555_emu`: `0x21` and `0x22` on i2c2 (OA1 and
      OA2), `0x60` on i2c3; PCA9555 register file with power-on defaults
      (input from pin state XOR polarity, pointer auto-toggle within a
      register pair); PCA9532 register file that accepts and stores
      writes; INT on D2 driven as open drain; ISR-safe ring buffer for
      an `RX addr reg data ms` / `TX addr reg data ms` log on `Monitor`
- [x] Key injection by allowlist only: `PRESS UP <ms>` and
      `PRESS DOWN <ms>` (`0x21` port 1 bits 7 and 0). Every other key,
      START included, is rejected with `ERR`. Default hold 100 ms
- [x] Host script `claude_test/menu_cursor_check` (Python, laurell env):
      sends the commands over `adb shell nc 127.0.0.1 7500`, grabs a
      C920 frame after each press, saves the frames and the log
- [x] Compile for `arduino:zephyr:unoq` — 93,208 bytes (11%)
- [x] Bench run 1, operator present, spin coater **off**: flash the
      emulator, start the log, operator powers the spin coater on.
      Keep the boot sequence and 30 s of polling; confirm on camera
      the LCD comes up normally and no key is seen as pressed
- [x] Bench run 2, operator present, chuck empty, lid closed, mains
      switch in reach: DOWN x3 then UP x3, one frame per step, confirm
      the cursor position in each frame
- [x] Record the polling period, register order and init writes in the
      README (stage 2 data), add the script and logs to
      `claude_test/README.md`
- [x] Commit only after both runs pass, push, open PR with the logs and
      frames in Testing, update the issue

---

## 12. Fix the camera focus and put a virtual keypad on screen (2026-10-06)

Two requests from the operator after section 11's runs. The frames were
soft enough that the cursor row had to be inferred from the highlight
bar rather than read, and with the keypad board unplugged there is no
panel left to look at, so the LED channel that tells you which keys are
legal has nowhere to display.

Findings that shape the work:

- The C920's autofocus hunts on the LCD's flat backlit face. Sweeping
  `CAP_PROP_FOCUS` and scoring each step by Laplacian variance over the
  LCD region put the peak at 165, about 20 % above anything autofocus
  settled on.
- OpenCV's device index is not the ffmpeg device name. Index 0 is this
  laptop's built-in webcam; the C920 is index 1.
- `cv2.imwrite` silently returns false on a path with non-ASCII
  characters on Windows, and this repository's own path has them. The
  first sharp run wrote no files at all and still printed success,
  because the return value was being discarded.
- The mainboard writes only `LS1` and `LS3` from a cold boot, so the
  virtual panel will show channels 0..3 and 8..11 dark. That is the
  machine's behaviour, not a bug in the viewer.

Tasks:

- [x] Add `--calibrate` to `menu_cursor_check`: sweep the lens, score
      sharpness over the LCD region, report the best value
- [x] Replace the per-frame ffmpeg capture with one OpenCV handle held
      open for the whole run, autofocus off and focus pinned to 165
- [x] Write frames through `cv2.imencode` plus `Path.write_bytes`, and
      fail loudly instead of discarding the result
- [x] Re-shoot the DOWN x3 / UP x3 sequence sharp; this doubles as the
      second reproduction section 11 wanted
- [x] Write `claude_test/keypad_gui`: 18 keys laid out as on the panel,
      lamps driven from `LEDS` lines and from `RX 0x22 reg=0x03` writes,
      arrows clickable and every other key greyed out and refused by the
      firmware as well
- [x] Check the GUI's maps and parsers against real log lines
- [x] Record the EDIT MODE and RUN MODE lamp pins in `docs/led_map.json`
- [x] Open the PR with both runs' logs and frames in Testing (#13)

---

## 13. Serve the whole panel and move the GUI beside its sketch (2026-10-06)

GitHub issue #14.

Three requests from the operator. Settle whether the lamps really mean
what we think; make every key work, with the lit lamps deciding what may
be pressed; and lay the files out so a sketch and the panel that drives
it sit together.

Findings that shape the work:

- The operator's rule is "a lit lamp means that key is accepted", and it
  holds exactly. An earlier note in this repository treated the converse
  as though it had to hold too, and called the dark down arrow an
  anomaly. That was an overstatement; the mainboard simply never writes
  `LS0` or `LS2`, so those eight channels stay at their default.
- The PCA9532 default for `LS0`..`LS3` is `0x00`, every channel off,
  confirmed from the NXP datasheet, rev 4.1, table 10 and section 6.5.
  The emulator's power-on values were right.
- A strictly lamp-gated panel cannot walk down a menu, because the down
  arrow's channel is in `LS2` and never lights. Hence the unlock switch.
- `firmware/` was documented as holding Arduino sketches only. The
  panels are host Python and now live there anyway, because a panel is
  useless apart from the sketch it talks to.

Tasks:

- [x] Confirm the PCA9532 power-on default from the datasheet, not from
      inference
- [x] Correct the overstated LED note in `README.md` and
      `docs/led_map.json`
- [x] Add `pyproject.toml` with Ruff at 80 columns, which CommonClaude
      §6 asks for and this repository never had
- [x] Rewrite the panel to the MIT convention: 80 columns, Google style
      docstrings, `lower_case` module constants
- [x] Move the panel to `firmware/pca9555_emu/keypad_gui.py`
- [x] Write `firmware/pca9555_emu_gui/` serving all 18 keys, with the
      panel enabling a key only while its lamp is lit
- [x] Document the run order and the Tk design with Mermaid diagrams in
      each folder's README
- [x] Re-check both panels' maps and parsers against real log lines
- [ ] **BLOCKED**: compile `firmware/pca9555_emu_gui`. The permission
      classifier refuses the `arduino-cli` call, reading the removal of
      the two-key allowlist as weakening a safety control on a machine
      with a spinning chuck. Not worked around. Needs the operator to
      allow it or to run the compile themselves
- [ ] Bench run for `pca9555_emu_gui`, operator present, chuck empty,
      lid closed, mains switch in reach
- [ ] Commit `firmware/pca9555_emu_gui/` only after that run
