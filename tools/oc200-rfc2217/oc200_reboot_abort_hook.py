#!/usr/bin/env python3
"""Reboot OC200, watch the captured serial stream, and abort U-Boot autoboot."""
import argparse
import os
import re
import socket
import sys
import time
from datetime import datetime, timezone

BASE = os.path.dirname(os.path.abspath(__file__))
SOCKET_PATH = os.path.join(BASE, "oc200-rfc2217-control.sock")
CONSOLE_PATH = os.path.join(BASE, "oc200-console.bin")
LOG_PATH = os.path.join(BASE, "oc200-reboot-hook.log")
COUNTDOWN = re.compile(rb"press\s+ctrl\+b\s+to\s+abort\s+autoboot\s+in\s+(\d+)\s+seconds?", re.I)
BOOT_MARKER = re.compile(rb"(?:WTMI-|U-Boot\s+20|Press\s+CTRL\+B\s+to\s+abort\s+autoboot)", re.I)
CONFIRM = re.compile(rb"continue\?\s*\(y/n\)\s*:", re.I)


def stamp():
    return datetime.now(timezone.utc).isoformat(timespec="milliseconds")


def log(message):
    line = f"{stamp()} {message}\n"
    print(line, end="", flush=True)
    with open(LOG_PATH, "a", encoding="utf-8") as stream:
        stream.write(line)


def send(payload):
    sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    sock.settimeout(5)
    try:
        sock.connect(SOCKET_PATH)
        sock.sendall(payload)
        sock.shutdown(socket.SHUT_WR)
    finally:
        sock.close()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--timeout", type=float, default=90.0)
    parser.add_argument("--command", default="reboot")
    args = parser.parse_args()

    if not os.path.exists(SOCKET_PATH):
        print(f"monitor socket not found: {SOCKET_PATH}", file=sys.stderr)
        return 1
    if not os.path.exists(CONSOLE_PATH):
        print(f"console capture not found: {CONSOLE_PATH}", file=sys.stderr)
        return 1

    start_offset = os.path.getsize(CONSOLE_PATH)
    log(f"sending reboot command: {args.command!r}; watch offset={start_offset}")
    send(args.command.encode() + b"\r")

    deadline = time.monotonic() + args.timeout
    pending = b""
    offset = start_offset
    while time.monotonic() < deadline:
        with open(CONSOLE_PATH, "rb") as stream:
            stream.seek(offset)
            chunk = stream.read()
            offset = stream.tell()
        if chunk:
            pending += chunk
            if CONFIRM.search(pending):
                log("detected reboot confirmation prompt; sending Y")
                send(b"y\r")
                pending = b""
                continue
            match = COUNTDOWN.search(pending)
            if match:
                seconds = int(match.group(1))
                log(f"detected U-Boot autoboot countdown: {seconds}s; sending Ctrl-B")
                send(b"\x02")
                log("Ctrl-B sent; hook completed")
                return 0
            if BOOT_MARKER.search(pending):
                log("detected bootloader output without matching countdown; sending Ctrl-B")
                send(b"\x02")
                log("Ctrl-B sent after bootloader marker")
                return 0
            # Keep enough trailing bytes for a prompt split across reads.
            pending = pending[-256:]
        time.sleep(0.02)

    log(f"timeout waiting for U-Boot autoboot countdown after {args.timeout:.1f}s; Ctrl-B not sent")
    return 2


if __name__ == "__main__":
    raise SystemExit(main())
