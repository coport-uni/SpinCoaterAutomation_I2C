"""Drive firmware/pca9555_emu and photograph the spin coater's LCD.

One-off bench harness for ToDo section 11. It holds a single connection
to the UNO Q's Monitor link, records everything the emulator prints, and
grabs a Logitech C920 frame at each step so the cursor position can be
read back afterwards.

Three modes:

    --observe 30            Watch only. Nothing is ever written to the
                            MCU, so this is what runs while the operator
                            powers the spin coater on.
    --steps DOWN,DOWN,UP    Inject keys, one frame per step.
    --calibrate             Sweep the lens and report the sharpest
                            focus value for FOCUS_VALUE below.

The emulator itself refuses every key except UP and DOWN; the allowlist
here is only so a typo fails on the host before it reaches the bench.

The camera is driven through OpenCV rather than ffmpeg for two reasons.
The C920's autofocus hunts on the LCD's flat backlit face and settles
soft, so focus is fixed manually; and holding one capture handle open
for the whole run removes the auto-exposure warm-up that a fresh
process pays on every single frame.

Usage:
    python menu_cursor_check.py --calibrate
    python menu_cursor_check.py --observe 30
    python menu_cursor_check.py --steps DOWN,DOWN,DOWN,UP,UP,UP
"""

import argparse
import datetime
import os
import pathlib
import subprocess
import sys
import threading
import time

import cv2

ADB = os.path.expandvars(
    r"%LOCALAPPDATA%\Arduino15\packages\arduino\tools\adb\32.0.0\adb.exe"
)

# DirectShow enumeration order on this PC: 0 is the laptop's built-in
# webcam, 1 is the C920 on its arm over the keypad. --calibrate reports
# the resolution it opened so a wrong index is obvious immediately.
CAMERA_INDEX = 1
FRAME_WIDTH = 1920
FRAME_HEIGHT = 1080

# Measured by --calibrate on 2026-10-06: sharpness peaked at 165, about
# 20 percent above anything autofocus settled on.
FOCUS_VALUE = 165

# Region of the frame that holds the LCD, used to score sharpness.
FOCUS_ROI = (750, 350, 1700, 950)
FOCUS_STEP = 5
FOCUS_MAX = 256

# Frames discarded after a lens move or on opening, before one is kept.
SETTLE_FRAMES = 6
WARMUP_FRAMES = 12

JPEG_QUALITY = 92

MONITOR_ENDPOINT = "127.0.0.1 7500"

# Only these reach the MCU. The firmware enforces the same list.
ALLOWED_KEYS = ("UP", "DOWN")

PRESS_HOLD_MS = 120

# Time for the mainboard to notice the key and redraw before the photo.
SETTLE_AFTER_PRESS_S = 1.0

# Time for the Monitor link to come up before the first command.
LINK_SETTLE_S = 1.0

# Time for a STATE reply to arrive before the next step.
REPLY_WAIT_S = 0.5


class Monitor:
    """One adb-tunnelled connection to the MCU's Monitor link."""

    def __init__(self, log_path):
        self.log_path = log_path
        self.lines = []
        self._proc = None
        self._thread = None

    def __enter__(self):
        self._proc = subprocess.Popen(
            [ADB, "shell", "nc " + MONITOR_ENDPOINT],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
        )
        self._thread = threading.Thread(target=self._reader, daemon=True)
        self._thread.start()
        return self

    def __exit__(self, *exc):
        if self._proc and self._proc.poll() is None:
            self._proc.terminate()
        return False

    def _reader(self):
        with open(self.log_path, "w", encoding="utf-8", newline="\n") as fh:
            for raw in self._proc.stdout:
                line = raw.decode("utf-8", "replace").rstrip("\r\n")
                stamp = time.strftime("%H:%M:%S")
                self.lines.append(line)
                fh.write(f"{stamp} {line}\n")
                fh.flush()
                print(f"  | {line}")

    def send(self, command):
        print(f"  > {command}")
        self._proc.stdin.write((command + "\n").encode("ascii"))
        self._proc.stdin.flush()


class Camera:
    """The C920, opened once with autofocus off and focus pinned."""

    def __init__(self, index=CAMERA_INDEX, focus=FOCUS_VALUE):
        self.index = index
        self.focus = focus
        self.cap = None

    def __enter__(self):
        self.cap = cv2.VideoCapture(self.index, cv2.CAP_DSHOW)
        if not self.cap.isOpened():
            raise SystemExit(f"camera index {self.index} would not open")
        self.cap.set(cv2.CAP_PROP_FRAME_WIDTH, FRAME_WIDTH)
        self.cap.set(cv2.CAP_PROP_FRAME_HEIGHT, FRAME_HEIGHT)
        self.cap.set(cv2.CAP_PROP_AUTOFOCUS, 0)
        self.cap.set(cv2.CAP_PROP_FOCUS, self.focus)
        for _ in range(WARMUP_FRAMES):
            self.cap.read()
        ok, frame = self.cap.read()
        if not ok:
            raise SystemExit(f"camera index {self.index} returned no frame")
        size = f"{frame.shape[1]}x{frame.shape[0]}"
        print(f"camera {self.index}: {size} focus={self.focus}")
        return self

    def __exit__(self, *exc):
        if self.cap is not None:
            self.cap.release()
        return False

    def grab(self):
        """Return one fresh frame, dropping the driver's stale buffer."""
        frame = None
        for _ in range(SETTLE_FRAMES):
            ok, frame = self.cap.read()
            if not ok:
                raise SystemExit("camera stopped returning frames")
        return frame

    def capture(self, path):
        """Write one fresh frame to *path*.

        cv2.imwrite goes through the C runtime's narrow-char file API and
        silently fails on a path with non-ASCII characters, which this
        repository's own directory has. Encoding in memory and writing
        the bytes from Python sidesteps that entirely.
        """
        ok, buf = cv2.imencode(
            ".jpg", self.grab(), [cv2.IMWRITE_JPEG_QUALITY, JPEG_QUALITY]
        )
        if not ok:
            raise SystemExit(f"could not encode a frame for {path.name}")
        path.write_bytes(buf.tobytes())
        print(f"  . frame -> {path.name}")


def sharpness(frame):
    """Score how crisp the LCD region is, by Laplacian variance."""
    x0, y0, x1, y1 = FOCUS_ROI
    gray = cv2.cvtColor(frame[y0:y1, x0:x1], cv2.COLOR_BGR2GRAY)
    return cv2.Laplacian(gray, cv2.CV_64F).var()


def calibrate(index):
    """Sweep the lens and report the focus value that reads sharpest."""
    with Camera(index=index, focus=0) as cam:
        best_focus, best_score = 0, -1.0
        for focus in range(0, FOCUS_MAX, FOCUS_STEP):
            cam.cap.set(cv2.CAP_PROP_FOCUS, focus)
            score = sharpness(cam.grab())
            print(f"focus={focus:3d}  sharpness={score:9.1f}")
            if score > best_score:
                best_focus, best_score = focus, score
        print(f"\nbest focus={best_focus} sharpness={best_score:.1f}")
        print(f"set FOCUS_VALUE = {best_focus} in this file")


def run_dir():
    """Make a timestamped directory for this run's frames and log."""
    now = datetime.datetime.now(datetime.UTC).astimezone()
    path = (
        pathlib.Path(__file__).parent / "runs" / now.strftime("%Y%m%d_%H%M%S")
    )
    (path / "frames").mkdir(parents=True, exist_ok=True)
    return path


def observe(cam, out, seconds):
    """Watch the bus without ever writing to the MCU."""
    cam.capture(out / "frames" / "00_start.jpg")
    print(f"observing for {seconds} s, nothing will be sent")
    time.sleep(seconds)
    cam.capture(out / "frames" / "99_end.jpg")


def inject(mon, cam, out, steps):
    """Press each key in turn, photographing the LCD after each one."""
    cam.capture(out / "frames" / "00_before.jpg")
    mon.send("STATE")
    time.sleep(REPLY_WAIT_S)

    for index, key in enumerate(steps, start=1):
        print(f"step {index}/{len(steps)}: {key}")
        mon.send(f"PRESS {key} {PRESS_HOLD_MS}")
        time.sleep(SETTLE_AFTER_PRESS_S)
        cam.capture(out / "frames" / f"{index:02d}_after_{key}.jpg")

    mon.send("STATE")
    time.sleep(REPLY_WAIT_S)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument(
        "--observe",
        type=int,
        metavar="SECONDS",
        help="watch the bus without ever writing to the MCU",
    )
    group.add_argument(
        "--steps",
        type=str,
        help="comma separated key names, each pressed once",
    )
    group.add_argument(
        "--calibrate",
        action="store_true",
        help="sweep the lens and report the sharpest focus value",
    )
    parser.add_argument(
        "--camera",
        type=int,
        default=CAMERA_INDEX,
        help=f"DirectShow camera index (default {CAMERA_INDEX})",
    )
    args = parser.parse_args()

    if args.calibrate:
        calibrate(args.camera)
        return

    if not os.path.exists(ADB):
        sys.exit(f"adb not found at {ADB}")

    steps = []
    if args.steps:
        steps = [s.strip().upper() for s in args.steps.split(",") if s.strip()]
        bad = [s for s in steps if s not in ALLOWED_KEYS]
        if bad:
            sys.exit(
                f"refusing to send {bad}; allowed: {', '.join(ALLOWED_KEYS)}"
            )

    out = run_dir()
    print(f"run directory: {out}")

    with Monitor(out / "monitor.log") as mon, Camera(index=args.camera) as cam:
        time.sleep(LINK_SETTLE_S)
        if args.observe:
            observe(cam, out, args.observe)
        else:
            inject(mon, cam, out, steps)

    print(f"done. frames and monitor.log are in {out}")


if __name__ == "__main__":
    main()
