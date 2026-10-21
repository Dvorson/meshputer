#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.9"
# dependencies = ["pyserial>=3.5"]
# ///
"""Talk to a Meshputer over its USB serial console.

    uv run tools/meshputer.py run status nodes "send ch0 hello"
    uv run tools/meshputer.py shot screen.png
    uv run tools/meshputer.py wait-link ready --timeout 60
    uv run tools/meshputer.py logs

The port is auto-detected (Espressif USB); override with --port.
"""

import argparse
import struct
import sys
import time
import zlib

import serial
from serial.tools import list_ports

ESPRESSIF_VID = 0x303A


def find_port():
    for p in list_ports.comports():
        if p.vid == ESPRESSIF_VID:
            return p.device
    for p in list_ports.comports():
        if "usbmodem" in p.device:
            return p.device
    sys.exit("No Meshputer found on USB; pass --port")


def open_port(port):
    s = serial.Serial()
    s.port = port
    s.baudrate = 115200
    s.timeout = 0.2
    # The ESP32-S3 USB-JTAG resets when RTS is asserted while DTR is not.
    # pyserial applies DTR before RTS on open, so clearing both would pass
    # through that state; keep DTR asserted and release RTS instead.
    s.dtr = True
    s.rts = False
    s.open()
    return s


def command(s, line, timeout=10.0, echo=True):
    """Send one console command; return its output lines (without log lines)."""
    # No reset_input_buffer() here: on macOS the flush can race with the
    # device's reply and drop it. Each command ends with exactly one "## " line.
    s.write((line + "\n").encode())
    out = []
    deadline = time.time() + timeout
    buf = b""
    while time.time() < deadline:
        buf += s.read(4096)
        while b"\n" in buf:
            raw, buf = buf.split(b"\n", 1)
            text = raw.decode(errors="replace").rstrip("\r")
            if text.startswith("## done"):
                return out, True
            if text.startswith("## error"):
                if echo:
                    print(text, file=sys.stderr)
                return out, False
            if text.startswith("[") and "] " in text:
                continue  # interleaved log line
            out.append(text)
            if echo:
                print(text)
    raise TimeoutError(f"no reply to {line!r}")


def write_png(path, width, height, rgb_rows):
    def chunk(kind, data):
        body = kind + data
        return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body) & 0xFFFFFFFF)

    raw = b"".join(b"\x00" + row for row in rgb_rows)
    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(raw, 9))
    png += chunk(b"IEND", b"")
    with open(path, "wb") as f:
        f.write(png)


def shot(s, path, scale=1, step=1):
    # A Tab5 frame (720x1280) is ~3.7 MB of hex; --step 2 sends a quarter of it.
    lines, ok = command(s, f"shot {step}", timeout=240, echo=False)
    if not ok or not lines or not lines[0].startswith("SHOT "):
        sys.exit("device did not return a frame")
    _, w, h, _fmt = lines[0].split()
    w, h = int(w), int(h)
    rows = []
    for hexrow in lines[1 : 1 + h]:
        data = bytes.fromhex(hexrow)
        rgb = bytearray()
        for i in range(0, len(data), 2):
            v = (data[i] << 8) | data[i + 1]  # RGB565, big-endian in the canvas
            r, g, b = (v >> 11) & 0x1F, (v >> 5) & 0x3F, v & 0x1F
            rgb += bytes(((r * 255) // 31, (g * 255) // 63, (b * 255) // 31))
        if scale > 1:  # nearest-neighbour keeps the pixel font crisp
            rgb = b"".join(bytes(rgb[i:i + 3]) * scale for i in range(0, len(rgb), 3))
        rows.extend([bytes(rgb)] * scale)
    if len(rows) != h * scale:
        sys.exit(f"frame truncated: {len(rows) // scale}/{h} rows")
    write_png(path, w * scale, h * scale, rows)
    print(f"saved {path} ({w * scale}x{h * scale})")


# Screens captured by `screenshots` (README / installer page), on demo data.
SCREENS = {
    "cardputer": [
        ("home.png", ["open home"]),
        ("chat.png", ["open chat 0"]),
        ("identity.png", ["open home", "open node !1a2b3c4d"]),
        ("nodes.png", ["open home", "open nodes"]),
        ("settings.png", ["open home", "open settings"]),
    ],
    # The Tab5 is drawn turned both ways; frames are saved at half size.
    "tab5": [
        ("tab5-chat-landscape.png", ["rotate 1", "open chat 0"]),
        ("tab5-node-landscape.png", ["rotate 1", "open node !1a2b3c4d"]),
        ("tab5-chats-portrait.png", ["rotate 0", "open chats"]),
        ("tab5-chat-portrait.png", ["rotate 0", "open chat 0"]),
    ],
}


def screenshots(s, out_dir, scale, board):
    import os
    os.makedirs(out_dir, exist_ok=True)
    command(s, "demo", echo=False)
    for name, steps in SCREENS[board]:
        for step in steps:
            _, ok = command(s, step, echo=False)
            if not ok:
                sys.exit(f"{step!r} failed")
        time.sleep(1.0)  # let the main loop redraw
        if board == "tab5":
            shot(s, os.path.join(out_dir, name), 1, 2)
        else:
            shot(s, os.path.join(out_dir, name), scale)
    if board == "tab5":
        command(s, "rotate auto", echo=False)
    command(s, "open home", echo=False)
    print("demo data stays loaded until the device reboots")


def wait_link(s, want, timeout):
    deadline = time.time() + timeout
    while time.time() < deadline:
        lines, _ = command(s, "status", echo=False)
        for line in lines:
            if line.startswith("link="):
                state = line.split()[0][5:]
                if state == want:
                    print(line)
                    return
        time.sleep(1)
    sys.exit(f"link did not reach {want!r} within {timeout}s")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port")
    sub = ap.add_subparsers(dest="cmd", required=True)
    p_run = sub.add_parser("run", help="run console commands")
    p_run.add_argument("commands", nargs="+")
    p_run.add_argument("--timeout", type=float, default=10)
    p_shot = sub.add_parser("shot", help="save the current screen as PNG")
    p_shot.add_argument("path")
    p_shot.add_argument("--scale", type=int, default=1, help="integer upscale, e.g. 3")
    p_shot.add_argument("--step", type=int, default=1, help="keep every n-th pixel (large panels)")
    p_wait = sub.add_parser("wait-link", help="wait until the link reaches a state")
    p_wait.add_argument("state")
    p_wait.add_argument("--timeout", type=float, default=60)
    p_screens = sub.add_parser("screenshots", help="capture the README screens on demo data")
    p_screens.add_argument("dir", nargs="?", default="docs/screens")
    p_screens.add_argument("--scale", type=int, default=3)
    p_screens.add_argument("--board", choices=sorted(SCREENS), default="cardputer")
    sub.add_parser("logs", help="stream log lines")
    args = ap.parse_args()

    s = open_port(args.port or find_port())
    if args.cmd == "run":
        ok = True
        for c in args.commands:
            print(f"$ {c}")
            _, good = command(s, c, timeout=args.timeout)
            ok = ok and good
        sys.exit(0 if ok else 1)
    elif args.cmd == "shot":
        shot(s, args.path, args.scale, args.step)
    elif args.cmd == "screenshots":
        screenshots(s, args.dir, args.scale, args.board)
    elif args.cmd == "wait-link":
        wait_link(s, args.state, args.timeout)
    elif args.cmd == "logs":
        try:
            while True:
                line = s.readline().decode(errors="replace").rstrip()
                if line:
                    print(line, flush=True)
        except KeyboardInterrupt:
            pass


if __name__ == "__main__":
    main()
