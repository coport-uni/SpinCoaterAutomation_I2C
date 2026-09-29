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
- [ ] Register the GitHub issue via `gh issue create`
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
- [ ] Register the GitHub issue via `gh issue create`
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
- [ ] Register the GitHub issue via `gh issue create`

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
- [ ] Open the PR via `gh pr create`

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
- [ ] Prove UNO Q slave mode with the A4/A5 to D20/D21 jumper self-test,
      keypad disconnected to avoid the `0x21` address clash
- [ ] Rewrite `firmware/pca9555_emu` for the UNO Q; the current draft is
      raw AVR TWI and does not apply
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
- [ ] Decide how to serve three addresses. Either break `i2c4` out of
      the high density connector, or find out whether the Zephyr STM32
      driver can register two targets on one controller using OA1 and
      OA2, or drop `0x60` and check that the mainboard tolerates a NAK
      there
- [x] Register the GitHub issue via `gh issue create` (#4)

This closes the largest unknown in the spec's risk table. The UNO Q can
answer as an I2C slave at the address the emulator needs, with both
`onReceive` and `onRequest` firing, so the board choice for stage 3 is
settled and no Nano is required.
