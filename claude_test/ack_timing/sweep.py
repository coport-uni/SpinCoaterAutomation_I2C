"""Sweep the emulator's i2c2 TIMINGR and count lost follow-ups (#21).

One-off bench script for claude_test/ack_timing/ack_timing.ino; delete
once #18 is settled. It never presses a key: the probe has no command
that could. For each setting it clears the counters, lets the
mainboard's 50 ms poll run for a few seconds, and saves the STAT table.
The default TIMINGR is restored at the end, and also on Ctrl-C.

Run with the spin coater on and sitting at Select Process, operator
present.
"""

import os
import queue
import re
import subprocess
import sys
import threading
import time
from pathlib import Path

adb_path = os.path.expandvars(
    r"%LOCALAPPDATA%\Arduino15\packages\arduino\tools\adb\32.0.0\adb.exe"
)
monitor_endpoint = "127.0.0.1 7500"
dwell_s = 5.0
reply_timeout_s = 5.0
sweep_bus = 2

timing_pattern = re.compile(r"^REGS i2c(\d) .*timingr=0x([0-9A-F]{8})")
stat_pattern = re.compile(
    r"^STAT (0x\w\w) cmd=(0x\w\w) n=(\d+) data=(\d+) read=(\d+) lost=(\d+)"
)
span_pattern = re.compile(
    r"^SPAN (0x\w\w) (even|odd) (read|lost) cmd_to_stop n=(\d+)"
    r"(?: min_ns=(\d+) avg_ns=(\d+) max_ns=(\d+))?"
)


class MonitorLink:
    """Line-oriented connection to the MCU Monitor through adb."""

    def __init__(self, log_path: Path) -> None:
        self.lines: queue.Queue[str] = queue.Queue()
        self.log = log_path.open("w", encoding="utf-8")
        self.proc = subprocess.Popen(
            [adb_path, "shell", "nc " + monitor_endpoint],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
        )
        threading.Thread(target=self._reader, daemon=True).start()

    def _reader(self) -> None:
        for raw in self.proc.stdout:
            line = raw.decode("ascii", "replace").rstrip()
            stamp = time.strftime("%H:%M:%S")
            self.log.write(f"{stamp} {line}\n")
            self.log.flush()
            self.lines.put(line)

    def send(self, command: str) -> None:
        self.log.write(f"> {command}\n")
        self.proc.stdin.write((command + "\n").encode("ascii"))
        self.proc.stdin.flush()

    def collect(self, until: str) -> list[str]:
        """Return lines up to and including one starting with ``until``."""
        got: list[str] = []
        deadline = time.monotonic() + reply_timeout_s
        while time.monotonic() < deadline:
            try:
                line = self.lines.get(timeout=0.2)
            except queue.Empty:
                continue
            got.append(line)
            if line.startswith(until):
                return got
        raise TimeoutError(f"no {until!r} line from the board")

    def close(self) -> None:
        self.proc.terminate()
        self.log.close()


def read_timing(link: MonitorLink) -> int:
    """Ask for REGS and return the swept bus's TIMINGR."""
    link.send("REGS")
    for line in link.collect("REGS i2c3"):
        match = timing_pattern.match(line)
        if match and int(match.group(1)) == sweep_bus:
            return int(match.group(2), 16)
    raise RuntimeError("REGS reply had no i2c2 line")


def measure(link: MonitorLink, label: str) -> dict:
    """Clear, dwell, and parse one STAT table."""
    link.send("CLEAR")
    link.collect("CLEAR ok")
    time.sleep(dwell_s)
    link.send("STAT")
    result = {"label": label, "stat": {}, "span": {}}
    for line in link.collect("STAT end"):
        if match := stat_pattern.match(line):
            addr, cmd, n, data, read, lost = match.groups()
            result["stat"][(addr, cmd)] = tuple(
                int(v) for v in (n, data, read, lost)
            )
        elif match := span_pattern.match(line):
            addr, parity, outcome, n, lo, avg, hi = match.groups()
            result["span"][(addr, parity, outcome)] = (
                int(n),
                int(lo or 0),
                int(avg or 0),
                int(hi or 0),
            )
    return result


def summarize(result: dict) -> str:
    """One line: the [00] and [01] outcomes of 0x21 and 0x22."""
    parts = [f"{result['label']:<16}"]
    for addr in ("0x21", "0x22"):
        for cmd in ("0x00", "0x01"):
            n, _, read, _ = result["stat"].get((addr, cmd), (0, 0, 0, 0))
            parts.append(f"{addr}[{cmd[-2:]}] {read:>3}/{n:<3}")
    lost_span = result["span"].get(("0x21", "even", "lost"))
    read_span = result["span"].get(("0x21", "odd", "read"))
    if lost_span:
        parts.append(f"even-lost stop {lost_span[2]} ns")
    if read_span:
        parts.append(f"odd-read stop {read_span[2]} ns")
    return "  ".join(parts)


def main() -> None:
    if not os.path.exists(adb_path):
        raise SystemExit(f"adb not found at {adb_path}")
    stamp = time.strftime("%Y%m%d_%H%M%S")
    log_path = Path(__file__).with_name(f"sweep_{stamp}.log")
    link = MonitorLink(log_path)
    default_timing = read_timing(link)
    print(f"default i2c{sweep_bus} TIMINGR = 0x{default_timing:08X}")

    settings = [("default", None)]
    settings += [(f"sdadel={n}", ("SDADEL", n)) for n in range(16)]
    settings += [
        ("presc=15 sdadel=0", ("PRESC_SDADEL", (15, 0))),
        ("presc=15 sdadel=15", ("PRESC_SDADEL", (15, 15))),
    ]

    print("columns: read/n for each pointer write, avg command-to-STOP")
    try:
        for label, change in settings:
            link.send(f"TIMING {sweep_bus} {default_timing:08X}")
            link.collect("REGS i2c3")
            if change is not None:
                kind, value = change
                if kind == "SDADEL":
                    link.send(f"SDADEL {sweep_bus} {value}")
                    link.collect("REGS i2c3")
                else:
                    link.send(f"PRESC {sweep_bus} {value[0]}")
                    link.collect("REGS i2c3")
                    link.send(f"SDADEL {sweep_bus} {value[1]}")
                    link.collect("REGS i2c3")
            print(summarize(measure(link, label)), flush=True)
    finally:
        link.send(f"TIMING {sweep_bus} {default_timing:08X}")
        try:
            link.collect("REGS i2c3")
            restored = read_timing(link)
            print(f"restored i2c{sweep_bus} TIMINGR = 0x{restored:08X}")
        finally:
            link.close()
            print(f"log: {log_path}")


if __name__ == "__main__":
    sys.exit(main())
