"""A virtual Laurell keypad on the PC, wired to firmware/pca9555_emu.

The real keypad board is unplugged and the UNO Q answers in its place,
so there is no longer a panel to look at. This puts one back on screen:
18 buttons with their indicator lamps, lit from the LED traffic the
mainboard is sending to the emulator right now.

What the lamps mean is the whole point. The mainboard lights a key's
lamp only while that key is a legal input in the current machine state,
so this window is a live list of the commands the spin coater will
accept, read straight off the bus.

Two sources feed the lamps:

    LEDS <16 chars>              PCA9532 channels 0..15, from 0x60
    RX 0x22 reg=0x03 data=0x..   EDIT MODE and RUN MODE, bits 0 and 1

Only the up and down arrows can be pressed. Every other key is drawn
greyed out, and the firmware refuses it by name as well, so a stray
click cannot reach START, STOP or VACUUM. That restriction is deliberate
and matches what has actually been verified on the bench.

Usage:
    python keypad_gui.py
"""

import json
import os
import pathlib
import queue
import re
import subprocess
import threading
import tkinter as tk
from tkinter import font as tkfont

ADB = os.path.expandvars(
    r"%LOCALAPPDATA%\Arduino15\packages\arduino\tools\adb\32.0.0\adb.exe"
)
MONITOR_ENDPOINT = "127.0.0.1 7500"

DOCS = pathlib.Path(__file__).resolve().parents[2] / "docs"

# Keys this GUI may send. The firmware enforces the same list.
ALLOWED_KEYS = ("ARROW_UP", "ARROW_DOWN")

PRESS_HOLD_DEFAULT_MS = 120
PRESS_HOLD_MIN_MS = 20
PRESS_HOLD_MAX_MS = 1000

# The two lamps that are not on the PCA9532. Identified from the stage 2
# log: the mainboard declares 0x22 port 1 all outputs and then drives
# bits 0 and 1 low, active low like every other lamp here.
LED_0X22_REG = 0x03
LED_0X22_BITS = {"EDIT_MODE": 0, "RUN_MODE": 1}

# Panel layout, (button name, row, column). It follows the grouping of
# docs/ButtonLayout.jpg rather than its exact geometry.
PANEL = (
    ("SELECT_PROCESS", 0, 0),
    ("INFO", 0, 1),
    ("EDIT_MODE", 0, 3),
    ("RUN_MODE", 0, 4),
    ("F1", 1, 0),
    ("F2", 1, 1),
    ("VACUUM", 1, 3),
    ("PAUSE", 1, 4),
    ("TAB_LEFT_PG_UP", 2, 0),
    ("TAB_RIGHT_PG_DN", 2, 1),
    ("START", 2, 3),
    ("STOP", 2, 4),
    ("ARROW_UP", 3, 1),
    ("FWD", 3, 3),
    ("REV", 3, 4),
    ("ARROW_LEFT", 4, 0),
    ("ARROW_DOWN", 4, 1),
    ("ARROW_RIGHT", 4, 2),
)

COLOURS = {
    "bg": "#1b1f24",
    "panel": "#252b33",
    "text": "#e6edf3",
    "muted": "#7d8795",
    "lamp_off": "#3a424d",
    "lamp_on": "#ffcc44",
    "lamp_pwm": "#d08a2a",
    "key": "#39414c",
    "key_live": "#2f6f4f",
    "key_edge": "#4a5361",
}

LED_RE = re.compile(r"^LEDS ([.*01]{16})")
RX22_RE = re.compile(r"^RX 0x22 reg=0x(\w{2}) data=0x(\w{2})")
LOG_LINES = 14
POLL_MS = 60


def load_maps():
    """Return (button by name, PCA9532 channel by button name)."""
    buttons = json.loads((DOCS / "button_map.json").read_text(encoding="utf-8"))
    leds = json.loads((DOCS / "led_map.json").read_text(encoding="utf-8"))
    by_name = {b["name"]: b for b in buttons["buttons"]}
    channel = {led["name"]: led["channel"] for led in leds["leds"]}
    return by_name, channel


class MonitorLink:
    """The adb-tunnelled Monitor connection, read on its own thread."""

    def __init__(self):
        self.lines = queue.Queue()
        self._proc = None

    def start(self):
        self._proc = subprocess.Popen(
            [ADB, "shell", "nc " + MONITOR_ENDPOINT],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
        )
        threading.Thread(target=self._reader, daemon=True).start()

    def _reader(self):
        for raw in self._proc.stdout:
            self.lines.put(raw.decode("utf-8", "replace").rstrip("\r\n"))

    def send(self, command):
        if self._proc is None or self._proc.poll() is not None:
            return False
        self._proc.stdin.write((command + "\n").encode("ascii"))
        self._proc.stdin.flush()
        return True

    def stop(self):
        if self._proc is not None and self._proc.poll() is None:
            self._proc.terminate()


class Keypad(tk.Tk):
    """The panel window."""

    def __init__(self, buttons, channels, link):
        super().__init__()
        self.buttons = buttons
        self.channels = channels
        self.link = link
        self.lamps = {}
        self.keys = {}

        self.title("Laurell keypad (emulated)")
        self.configure(bg=COLOURS["bg"])
        self.hold_ms = tk.IntVar(value=PRESS_HOLD_DEFAULT_MS)

        self._build()
        self.protocol("WM_DELETE_WINDOW", self._close)
        self.after(POLL_MS, self._drain)
        self.link.send("STATE")

    def _build(self):
        label_font = tkfont.Font(family="Segoe UI", size=9)
        mono = tkfont.Font(family="Consolas", size=9)

        head = tk.Label(
            self,
            text="Lamps are lit by the mainboard. A lit key is one the "
            "spin coater will accept right now.",
            bg=COLOURS["bg"],
            fg=COLOURS["muted"],
            font=label_font,
        )
        head.grid(row=0, column=0, padx=12, pady=(12, 6), sticky="w")

        panel = tk.Frame(self, bg=COLOURS["panel"], padx=14, pady=14)
        panel.grid(row=1, column=0, padx=12, sticky="ew")

        for name, row, col in PANEL:
            self._build_key(panel, name, row, col, label_font)

        controls = tk.Frame(self, bg=COLOURS["bg"])
        controls.grid(row=2, column=0, padx=12, pady=(10, 4), sticky="w")

        tk.Label(
            controls,
            text="hold (ms)",
            bg=COLOURS["bg"],
            fg=COLOURS["muted"],
            font=label_font,
        ).pack(side="left")
        tk.Spinbox(
            controls,
            from_=PRESS_HOLD_MIN_MS,
            to=PRESS_HOLD_MAX_MS,
            increment=10,
            width=6,
            textvariable=self.hold_ms,
            font=label_font,
        ).pack(side="left", padx=(6, 16))
        tk.Button(
            controls,
            text="STATE",
            command=lambda: self.link.send("STATE"),
            font=label_font,
        ).pack(side="left")

        self.status = tk.Label(
            self,
            text="connecting...",
            bg=COLOURS["bg"],
            fg=COLOURS["muted"],
            font=label_font,
        )
        self.status.grid(row=3, column=0, padx=12, sticky="w")

        self.log = tk.Text(
            self,
            height=LOG_LINES,
            width=74,
            bg="#12161a",
            fg=COLOURS["muted"],
            font=mono,
            relief="flat",
            state="disabled",
        )
        self.log.grid(row=4, column=0, padx=12, pady=(4, 12), sticky="ew")

    def _build_key(self, parent, name, row, col, label_font):
        info = self.buttons.get(name, {})
        live = name in ALLOWED_KEYS

        cell = tk.Frame(parent, bg=COLOURS["panel"])
        cell.grid(row=row, column=col, padx=6, pady=6)

        lamp = tk.Canvas(
            cell, width=14, height=14, bg=COLOURS["panel"], highlightthickness=0
        )
        dot = lamp.create_oval(
            2, 2, 12, 12, fill=COLOURS["lamp_off"], outline=COLOURS["key_edge"]
        )
        lamp.pack()
        self.lamps[name] = (lamp, dot)

        key = tk.Button(
            cell,
            text=info.get("label", name),
            width=14,
            font=label_font,
            bg=COLOURS["key_live"] if live else COLOURS["key"],
            fg=COLOURS["text"] if live else COLOURS["muted"],
            activebackground=COLOURS["key_live"],
            relief="flat",
            state="normal" if live else "disabled",
            disabledforeground=COLOURS["muted"],
            command=(lambda n=name: self._press(n)) if live else None,
        )
        key.pack(pady=(3, 0))
        self.keys[name] = key

    def _press(self, name):
        short = "UP" if name == "ARROW_UP" else "DOWN"
        if self.link.send(f"PRESS {short} {self.hold_ms.get()}"):
            self._write_log(f">> PRESS {short} {self.hold_ms.get()}")
        else:
            self._write_log(">> link is down")

    def _set_lamp(self, name, state):
        entry = self.lamps.get(name)
        if entry is None:
            return
        canvas, dot = entry
        canvas.itemconfigure(dot, fill=COLOURS[state])

    def _apply_leds(self, pattern):
        """Light the PCA9532 lamps from one LEDS line."""
        for name, channel in self.channels.items():
            mark = pattern[channel]
            if mark == "*":
                self._set_lamp(name, "lamp_on")
            elif mark in "01":
                self._set_lamp(name, "lamp_pwm")
            else:
                self._set_lamp(name, "lamp_off")

    def _apply_0x22(self, reg, value):
        """Light EDIT MODE and RUN MODE from an 0x22 output write."""
        if reg != LED_0X22_REG:
            return
        for name, bit in LED_0X22_BITS.items():
            lit = (value >> bit) & 1 == 0
            self._set_lamp(name, "lamp_on" if lit else "lamp_off")

    def _drain(self):
        changed = False
        while True:
            try:
                line = self.link.lines.get_nowait()
            except queue.Empty:
                break
            changed = True
            self._consume(line)
        if changed:
            self.status.configure(text="link up")
        self.after(POLL_MS, self._drain)

    def _consume(self, line):
        match = LED_RE.match(line)
        if match:
            self._apply_leds(match.group(1))
            self._write_log(line)
            return

        match = RX22_RE.match(line)
        if match:
            self._apply_0x22(int(match.group(1), 16), int(match.group(2), 16))
            self._write_log(line)
            return

        # The 50 ms poll would bury everything else; keep the rest.
        if not line.startswith(("TX ", "POLL ")):
            self._write_log(line)

    def _write_log(self, line):
        self.log.configure(state="normal")
        self.log.insert("end", line + "\n")
        self.log.see("end")
        self.log.configure(state="disabled")

    def _close(self):
        self.link.stop()
        self.destroy()


def main():
    if not os.path.exists(ADB):
        raise SystemExit(f"adb not found at {ADB}")
    buttons, channels = load_maps()
    link = MonitorLink()
    link.start()
    Keypad(buttons, channels, link).mainloop()


if __name__ == "__main__":
    main()
