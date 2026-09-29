# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working
with code in this repository.

## Shared conventions

The project-wide ruleset lives in the `CommonClaude` submodule and is
imported here in full:

@external/CommonClaude/CLAUDE.md

Read that file first. The sections below are **project-level overrides**;
per §1 Rule Priority, they win wherever they disagree with the shared
ruleset. Everything not mentioned below applies unchanged — in
particular §3 Debug File Management, §4 Task Management, §5.1 the
Verification Gate, §9/§10 Learned Patterns, §11 Commit Messages,
§12 Branching, §14 Versioning, and §15 Pull Requests.

Enforcement hooks are configured in
[.claude/settings.json](.claude/settings.json) and point at the scripts
under `external/CommonClaude/.claude/hooks/`, so updating the submodule
updates the hooks.

### Updating the shared ruleset

```sh
git submodule update --remote external/CommonClaude
git add external/CommonClaude
git commit -m "chore(claude): update CommonClaude submodule"
```

A fresh clone needs `git submodule update --init --recursive`, otherwise
`external/CommonClaude/` is empty and every hook fails.

---

## 1. Project overview

Reverse-engineering the Laurell spin coater keypad board
(Ellenby 09-0128-00) over I2C, and ultimately replacing it with a
PCA9555 slave emulator that drives the spin coater. The specification is
[docs/spincoater_keypad_spec.md](docs/spincoater_keypad_spec.md); current
status is in [README.md](README.md).

| Path | Contents |
|---|---|
| `firmware/` | Production Arduino sketches. This is the deliverable. |
| `claude_test/` | One-off probe sketches and logs (§3 scratch area). |
| `docs/` | Specification, button/LED maps, reference photos. |
| `external/CommonClaude/` | Shared conventions submodule. Never edit. |

There is no `tests/` directory. The shared §3 rule still holds: if one is
created, it is for CI-quality tests only, and probes stay in
`claude_test/`.

---

## 2. Override — language

The shared §2 Language rule requires English everywhere. **In this
repository, Korean is the language of prose documentation**: `README.md`,
`docs/`, and `claude_test/README.md` are already written in Korean and
stay that way. Match the language of the file being edited.

English remains required for:

- code comments and identifiers,
- commit messages and PR titles/bodies,
- GitHub issue titles and bodies.

---

## 3. Override — language of the codebase

The shared ruleset on `main` is written for Python. The CommonClaude
branch `feat/c-language-support` (`c18da89`) carries the C convention
instead, but that branch is 19 commits behind `main` and lacks the
Verification Gate, the MCP/`gh` rules, and the Windows hook-path fix.
The submodule therefore stays on `main`, and the C-language sections of
`c18da89` are reproduced here as overrides.

This repository is **Arduino C++** (`.ino`), so the following replaces
the shared §2 Python naming table:

| Element  | Style               | Example                    |
|----------|---------------------|----------------------------|
| Variable | `snake_case`        | `button_mask`              |
| Function | `snake_case`        | `read_input_port`          |
| Type     | `PascalCase` / `_t` | `KeyEvent` / `key_event_t` |
| Macro    | `UPPER_SNAKE_CASE`  | `MAX_BUFFER_SIZE`          |
| Constant | `UPPER_SNAKE_CASE`  | `SETTLE_MID_MS`            |
| File     | `lower_snake`       | `pca9555_emu.ino`          |

Arduino framework identifiers (`Wire.beginTransmission`, `setup`,
`loop`, …) keep their upstream spelling.

### Documentation

Doxygen replaces PEP 257. All public functions and types must have
**Doxygen comment blocks** (`/** ... */`). A block states **what** and
**why**, not **how**, and includes `@brief`, `@param`, and `@return`
when applicable.

### TODO format

The C block form replaces the Python `#` form:

```c
/* TODO: (@owner) Implement 2-step predictor-corrector
 * for stability. Adams-Bashforth causes shocks.
 */
```

### Unchanged from the shared §2

80-column limit, 4-space indent, one statement per line, operators on
the left of continuation lines, and complete-sentence comments.

Formatting follows
[external/CommonClaude/.clang-format](external/CommonClaude/.clang-format)
(LLVM base, 80 columns, 4-space indent) — identical on `main` and on the
C branch.

### Exceptions

Per shared §8, scripts under `claude_test/` are exempt from the
80-column limit and from **mandatory Doxygen blocks** (the C branch's
wording; `main` says "docstrings"). The Verification Gate is never
waived.

---

## 4. Override — linting and build verification

The C branch mandates **clang-format** and **cppcheck** in place of the
Ruff rule in shared §6. Ruff does not apply here — there is no Python —
and `post-write-lint.sh` is a no-op on `.ino` files.

Neither C tool is installed on this machine yet:

```sh
winget install LLVM.LLVM          # clang-format
winget install Cppcheck.Cppcheck  # cppcheck
```

Once installed, run both before every commit:

```sh
clang-format --dry-run --Werror \
    -style=file:external/CommonClaude/.clang-format <file>.ino
cppcheck --enable=warning,style --error-exitcode=1 <file>.ino
```

Use `clang-format -i` to auto-format. Note that `cppcheck` does not
understand the Arduino preprocessor prelude; treat its unknown-macro
complaints as noise and keep the `warning,style` findings.

Until both tools are present, the compile check is the standing
substitute and is **required before every commit**:

```sh
"/c/Program Files/Arduino IDE/resources/app/lib/backend/resources/arduino-cli.exe" \
    compile --fqbn arduino:zephyr:unoq firmware/<sketch>
```

A clean compile is necessary but **not** sufficient — see below.

---

## 5. Verification Gate — this is hardware work

Shared §5.1 applies with no waiver, and it is the rule that matters most
here. Every sketch in `firmware/` and `claude_test/` drives real
hardware: the keypad board, and eventually the spin coater itself.

1. A sketch is verified only by a **run on the actual board with the
   operator present**. A clean `arduino-cli compile` is evidence about
   the code, not about the hardware.
2. **Verify before `git add`.** No commit, push, PR, or merge of a sketch
   that has not been run.
3. Paste the real serial output into the PR's `## Testing` section, with
   the device and the date. See `claude_test/README.md` for how to read
   the MCU serial through the Router Bridge / adb path — the UNO Q does
   not expose `Serial` on a host COM port directly.
4. **Never energize the spin coater or inject key events on your own
   initiative.** Ask, wait for confirmation, and keep the operator at the
   bench. Stage 3 (emulation) drives a machine with a spinning chuck.
5. Report honestly. "Compiled, not yet run on the bench" in a PR body is
   correct and blocks the merge; an optimistic summary is a defect.

---

## 6. Task workflow reminder

Shared §4 is mandatory here as well: `ToDo.md` entry → user confirmation
→ `gh issue create` → working branch → work → **hardware verification**
→ `gh issue edit` → `gh pr create`. `ToDo.md` is append-only; only
checkbox flips and commit/issue links may be added to existing lines.
