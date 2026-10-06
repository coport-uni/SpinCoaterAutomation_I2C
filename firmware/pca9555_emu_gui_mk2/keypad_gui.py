"""Virtual Laurell keypad, gated by the mainboard's own lamps.

The whole panel is drawn, and a key is clickable only while its lamp is
lit. The mainboard lights a key's lamp when that key is a legal input in
the current machine state, so the machine itself decides what this
window will let through. That is a better gate than a list written here,
because it follows the spin coater rather than guessing at it.

It is not a safety interlock. The lid, the machine's own logic and the
mains switch are what stand between a key press and harm, exactly as
they did when the real keypad was plugged in.

With ``pca9555_emu_gui_mk2`` the gate should be complete. The first
emulator lost the ``LS0`` and ``LS2`` data bytes (#18), so channels
0..3 and 8..11, the down arrow among them, stayed dark and a strictly
lamp-gated panel could not walk down a menu. Mark 2 fixes the target's
data-hold delay, and in ``claude_test/ack_timing`` both registers then
arrived. Whether every lamp now matches the real keypad has still to be
checked on the bench, so "unlock all keys" stays, off by default.

Two lines of the emulator's output feed the lamps, and ``STATE`` prints
both, so a window opened after the mainboard has set them still sees
them::

    LEDS <16 chars> psc0=.. pwm0=.. psc1=.. pwm1=..   from 0x60
    LED22 out=0x.. cfg=0x..          EDIT MODE and RUN MODE, bits 0 and 1

Pair this with
``firmware/pca9555_emu_gui_mk2/pca9555_emu_gui_mk2.ino``, which serves
all 18 keys. The arrows-only ``firmware/pca9555_emu`` will answer
``ERR`` to everything else.
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

adb_path = os.path.expandvars(
    r"%LOCALAPPDATA%\Arduino15\packages\arduino\tools\adb\32.0.0\adb.exe"
)

monitor_endpoint = "127.0.0.1 7500"

docs_dir = pathlib.Path(__file__).resolve().parents[2] / "docs"

# docs/button_map.json's long names to the host protocol's short ones.
key_names = {
    "TAB_LEFT_PG_UP": "PGUP",
    "TAB_RIGHT_PG_DN": "PGDN",
    "FWD": "FWD",
    "ARROW_RIGHT": "RIGHT",
    "F2": "F2",
    "F1": "F1",
    "VACUUM": "VACUUM",
    "SELECT_PROCESS": "SELECT",
    "ARROW_DOWN": "DOWN",
    "REV": "REV",
    "ARROW_LEFT": "LEFT",
    "PAUSE": "PAUSE",
    "STOP": "STOP",
    "START": "START",
    "INFO": "INFO",
    "ARROW_UP": "UP",
    "EDIT_MODE": "EDIT",
    "RUN_MODE": "RUN",
}

press_hold_default_ms = 120
press_hold_min_ms = 20
press_hold_max_ms = 1000

# The two lamps that are not on the PCA9532. Identified from the stage 2
# log: the mainboard declares 0x22 port 1 all outputs, then drives bits
# 0 and 1 low, active low like every other lamp here.
led_0x22_bits = {"EDIT_MODE": 0, "RUN_MODE": 1}

# A PCA9532 blink output is on for PWM/256 of its period, so a duty
# register of zero keeps every channel that selects it dark. The
# mainboard writes PWM0 = 0x00 at boot.
pwm_duty_off = 0x00

# Panel layout, as (button name, row, column), placed where each key
# sits on the real overlay, docs/ButtonLayout.jpg: the mode keys down
# the left, the arrow cross in the middle, the page keys on the right,
# and the motion keys along the bottom, set apart by an empty row.
panel_layout = (
    ("SELECT_PROCESS", 0, 0),
    ("VACUUM", 0, 2),
    ("F1", 0, 3),
    ("F2", 0, 4),
    ("RUN_MODE", 1, 0),
    ("EDIT_MODE", 2, 0),
    ("ARROW_UP", 2, 2),
    ("TAB_LEFT_PG_UP", 2, 4),
    ("INFO", 3, 0),
    ("ARROW_LEFT", 3, 1),
    ("ARROW_DOWN", 3, 2),
    ("ARROW_RIGHT", 3, 3),
    ("TAB_RIGHT_PG_DN", 3, 4),
    ("START", 5, 0),
    ("STOP", 5, 1),
    ("PAUSE", 5, 2),
    ("REV", 5, 3),
    ("FWD", 5, 4),
)
panel_gap_row = 4
panel_gap_px = 18

colours = {
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

led_pattern = re.compile(
    r"^LEDS ([.*01]{16}) psc0=0x\w{2} pwm0=0x(\w{2})"
    r" psc1=0x\w{2} pwm1=0x(\w{2})"
)
led_0x22_pattern = re.compile(r"^LED22 out=0x(\w{2}) cfg=0x(\w{2})")

log_height_lines = 14
drain_period_ms = 60


def load_maps() -> tuple[dict, dict]:
    """Read the button and LED maps that describe the real panel.

    Returns:
        A pair of dictionaries. The first maps a button name to its
        entry in ``docs/button_map.json``; the second maps a button
        name to the PCA9532 channel that drives its lamp.
    """
    buttons = json.loads(
        (docs_dir / "button_map.json").read_text(encoding="utf-8")
    )
    leds = json.loads((docs_dir / "led_map.json").read_text(encoding="utf-8"))
    by_name = {entry["name"]: entry for entry in buttons["buttons"]}
    channel = {led["name"]: led["channel"] for led in leds["leds"]}
    return by_name, channel


class MonitorLink:
    """One adb-tunnelled connection to the MCU's Monitor link.

    The UNO Q does not expose the MCU's serial port to the host. The
    Router Bridge puts it on a socket on the board's Linux side
    instead, which adb reaches.
    """

    def __init__(self) -> None:
        """Prepare the link without opening it yet."""
        self.lines: queue.Queue[str] = queue.Queue()
        self._proc: subprocess.Popen | None = None

    def start(self) -> None:
        """Open the connection and begin reading it on a thread."""
        self._proc = subprocess.Popen(
            [adb_path, "shell", "nc " + monitor_endpoint],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
        )
        threading.Thread(target=self._reader, daemon=True).start()

    def _reader(self) -> None:
        """Feed every line the emulator prints into the queue."""
        for raw in self._proc.stdout:
            self.lines.put(raw.decode("utf-8", "replace").rstrip("\r\n"))

    def send(self, command: str) -> bool:
        """Send one command line to the emulator.

        Args:
            command: The line to send, without its terminator.

        Returns:
            True when the line went out, False when the link is down.
        """
        if self._proc is None or self._proc.poll() is not None:
            return False
        self._proc.stdin.write((command + "\n").encode("ascii"))
        self._proc.stdin.flush()
        return True

    def stop(self) -> None:
        """Close the connection if it is still open."""
        if self._proc is not None and self._proc.poll() is None:
            self._proc.terminate()


class Keypad(tk.Tk):
    """The panel window.

    Tk is single threaded, so the reader thread only ever puts lines on
    a queue; every widget call happens here, on the main thread, from
    the periodic drain.
    """

    def __init__(
        self, buttons: dict, channels: dict, link: MonitorLink
    ) -> None:
        """Build the window and start watching the link.

        Args:
            buttons: Button name to ``docs/button_map.json`` entry.
            channels: Button name to PCA9532 channel.
            link: An already started connection to the emulator.
        """
        super().__init__()
        self.buttons = buttons
        self.channels = channels
        self.link = link
        self.lamps: dict[str, tuple[tk.Canvas, int]] = {}
        self.keys: dict[str, tk.Button] = {}
        self.lit: dict[str, bool] = dict.fromkeys(key_names, False)

        self.title("Laurell keypad (emulated, mk2)")
        self.configure(bg=colours["bg"])
        self.hold_ms = tk.IntVar(value=press_hold_default_ms)
        self.unlocked = tk.BooleanVar(value=False)

        self._build()
        self.protocol("WM_DELETE_WINDOW", self._close)
        self.after(drain_period_ms, self._drain)
        self.link.send("STATE")

    def _build(self) -> None:
        """Lay out the heading, the panel, the controls and the log."""
        label_font = tkfont.Font(family="Segoe UI", size=9)
        mono_font = tkfont.Font(family="Consolas", size=9)

        tk.Label(
            self,
            text="A key is clickable only while its lamp is lit. The "
            "mainboard decides which those are.",
            bg=colours["bg"],
            fg=colours["muted"],
            font=label_font,
        ).grid(row=0, column=0, padx=12, pady=(12, 6), sticky="w")

        panel = tk.Frame(self, bg=colours["panel"], padx=14, pady=14)
        panel.grid(row=1, column=0, padx=12, sticky="ew")

        for name, row, col in panel_layout:
            self._build_key(panel, name, row, col, label_font)
        # An empty grid row has no height of its own.
        panel.grid_rowconfigure(panel_gap_row, minsize=panel_gap_px)

        self._build_controls(label_font)

        self.status = tk.Label(
            self,
            text="connecting...",
            bg=colours["bg"],
            fg=colours["muted"],
            font=label_font,
        )
        self.status.grid(row=3, column=0, padx=12, sticky="w")

        self.log = tk.Text(
            self,
            height=log_height_lines,
            width=74,
            bg="#12161a",
            fg=colours["muted"],
            font=mono_font,
            relief="flat",
            state="disabled",
        )
        self.log.grid(row=4, column=0, padx=12, pady=(4, 12), sticky="ew")
        self._refresh_keys()

    def _build_controls(self, label_font: tkfont.Font) -> None:
        """Add the hold time spinner, the unlock switch and STATE."""
        controls = tk.Frame(self, bg=colours["bg"])
        controls.grid(row=2, column=0, padx=12, pady=(10, 4), sticky="w")

        tk.Label(
            controls,
            text="hold (ms)",
            bg=colours["bg"],
            fg=colours["muted"],
            font=label_font,
        ).pack(side="left")
        tk.Spinbox(
            controls,
            from_=press_hold_min_ms,
            to=press_hold_max_ms,
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
        tk.Checkbutton(
            controls,
            text="unlock all keys",
            variable=self.unlocked,
            command=self._refresh_keys,
            bg=colours["bg"],
            fg=colours["muted"],
            selectcolor=colours["panel"],
            activebackground=colours["bg"],
            activeforeground=colours["text"],
            font=label_font,
        ).pack(side="left", padx=(16, 0))

    def _build_key(
        self,
        parent: tk.Frame,
        name: str,
        row: int,
        col: int,
        label_font: tkfont.Font,
    ) -> None:
        """Add one key and its lamp to the panel.

        Args:
            parent: The panel frame.
            name: Button name as used in ``docs/button_map.json``.
            row: Grid row within the panel.
            col: Grid column within the panel.
            label_font: Font shared by every key face.
        """
        info = self.buttons.get(name, {})

        cell = tk.Frame(parent, bg=colours["panel"])
        cell.grid(row=row, column=col, padx=6, pady=6)

        lamp = tk.Canvas(
            cell,
            width=14,
            height=14,
            bg=colours["panel"],
            highlightthickness=0,
        )
        dot = lamp.create_oval(
            2,
            2,
            12,
            12,
            fill=colours["lamp_off"],
            outline=colours["key_edge"],
        )
        lamp.pack()
        self.lamps[name] = (lamp, dot)

        key = tk.Button(
            cell,
            text=info.get("label", name),
            width=14,
            font=label_font,
            bg=colours["key"],
            fg=colours["text"],
            activebackground=colours["key_live"],
            relief="flat",
            disabledforeground=colours["muted"],
            command=lambda n=name: self._press(n),
        )
        key.pack(pady=(3, 0))
        self.keys[name] = key

    def _refresh_keys(self) -> None:
        """Enable each key according to its lamp, or to the switch."""
        unlocked = self.unlocked.get()
        for name, key in self.keys.items():
            is_live = unlocked or self.lit.get(name, False)
            key.configure(
                state="normal" if is_live else "disabled",
                bg=colours["key_live"] if is_live else colours["key"],
            )

    def _press(self, name: str) -> None:
        """Ask the emulator to hold one key down.

        Args:
            name: Button name as used in ``docs/button_map.json``.
        """
        command = f"PRESS {key_names[name]} {self.hold_ms.get()}"
        if self.link.send(command):
            self._write_log(">> " + command)
        else:
            self._write_log(">> link is down")

    def _set_lamp(self, name: str, state: str) -> None:
        """Paint one lamp and record whether it counts as lit.

        Args:
            name: Button name whose lamp to paint.
            state: A key of ``colours``, such as ``"lamp_on"``.
        """
        entry = self.lamps.get(name)
        if entry is None:
            return
        canvas, dot = entry
        canvas.itemconfigure(dot, fill=colours[state])
        self.lit[name] = state != "lamp_off"

    def _apply_leds(self, pattern: str, pwm0: int, pwm1: int) -> None:
        """Light the PCA9532 lamps from one LEDS line.

        Args:
            pattern: The 16 character field of a ``LEDS`` line.
            pwm0: The PWM0 duty register.
            pwm1: The PWM1 duty register.
        """
        duty = {"0": pwm0, "1": pwm1}
        for name, channel in self.channels.items():
            mark = pattern[channel]
            if mark == "*":
                self._set_lamp(name, "lamp_on")
            elif mark in duty and duty[mark] != pwm_duty_off:
                self._set_lamp(name, "lamp_pwm")
            else:
                self._set_lamp(name, "lamp_off")

    def _apply_0x22(self, output: int, config: int) -> None:
        """Light EDIT MODE and RUN MODE from 0x22 port 1.

        Args:
            output: The port's output latch. The lamps are active low.
            config: The port's configuration. A bit set to 1 makes the
                pin an input, which leaves its lamp undriven.
        """
        for name, bit in led_0x22_bits.items():
            is_output = (config >> bit) & 1 == 0
            is_low = (output >> bit) & 1 == 0
            is_lit = is_output and is_low
            self._set_lamp(name, "lamp_on" if is_lit else "lamp_off")

    def _drain(self) -> None:
        """Consume whatever the reader thread queued, then requeue."""
        saw_any = False
        while True:
            try:
                line = self.link.lines.get_nowait()
            except queue.Empty:
                break
            saw_any = True
            self._consume(line)
        if saw_any:
            self.status.configure(text="link up")
            self._refresh_keys()
        self.after(drain_period_ms, self._drain)

    def _consume(self, line: str) -> None:
        """Route one line from the emulator.

        Args:
            line: One line as the emulator printed it.
        """
        match = led_pattern.match(line)
        if match:
            self._apply_leds(
                match.group(1),
                int(match.group(2), 16),
                int(match.group(3), 16),
            )
            self._write_log(line)
            return

        match = led_0x22_pattern.match(line)
        if match:
            self._apply_0x22(int(match.group(1), 16), int(match.group(2), 16))
            self._write_log(line)
            return

        # The 50 ms poll would bury everything else; keep the rest.
        if not line.startswith(("TX ", "POLL ")):
            self._write_log(line)

    def _write_log(self, line: str) -> None:
        """Append one line to the log pane.

        Args:
            line: Text to append.
        """
        self.log.configure(state="normal")
        self.log.insert("end", line + "\n")
        self.log.see("end")
        self.log.configure(state="disabled")

    def _close(self) -> None:
        """Drop the link and close the window."""
        self.link.stop()
        self.destroy()


def main() -> None:
    """Open the link and run the panel until the window is closed."""
    if not os.path.exists(adb_path):
        raise SystemExit(f"adb not found at {adb_path}")
    buttons, channels = load_maps()
    link = MonitorLink()
    link.start()
    Keypad(buttons, channels, link).mainloop()


if __name__ == "__main__":
    main()
