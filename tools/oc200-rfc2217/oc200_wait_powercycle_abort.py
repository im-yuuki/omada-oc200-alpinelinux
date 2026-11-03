#!/usr/bin/env python3
"""Wait for a physical power cycle, then abort the U-Boot autoboot countdown."""
import os
import re
import socket
import time
from pathlib import Path

BASE = Path(__file__).resolve().parent
SOCKET_PATH = BASE / "oc200-rfc2217-control.sock"
CONSOLE_PATH = BASE / "oc200-console.bin"
LOG_PATH = BASE / "oc200-powercycle-hook.log"
COUNTDOWN = re.compile(rb"press\s+ctrl\+b\s+to\s+abort\s+autoboot\s+in\s+(\d+)\s+seconds?", re.I)
BOOT_MARKER = re.compile(rb"(?:WTMI-|U-Boot\s+20|Press\s+CTRL\+B\s+to\s+abort\s+autoboot)", re.I)


def log(message):
    line = f"{time.strftime('%Y-%m-%dT%H:%M:%S%z')} {message}\n"
    print(line, end="", flush=True)
    with LOG_PATH.open("a", encoding="utf-8") as stream:
        stream.write(line)


def send(payload):
    with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as sock:
        sock.settimeout(5)
        sock.connect(str(SOCKET_PATH))
        sock.sendall(payload)
        sock.shutdown(socket.SHUT_WR)


def main():
    offset = CONSOLE_PATH.stat().st_size
    log(f"waiting for physical power cycle from console offset {offset}")
    pending = b""
    while True:
        with CONSOLE_PATH.open("rb") as stream:
            stream.seek(offset)
            chunk = stream.read()
            offset = stream.tell()
        if chunk:
            pending += chunk
            match = COUNTDOWN.search(pending)
            if match:
                log(f"detected U-Boot countdown {match.group(1).decode()}s; sending Ctrl-B")
                send(b"\x02")
                return 0
            if BOOT_MARKER.search(pending):
                log("detected bootloader output; sending Ctrl-B")
                send(b"\x02")
                return 0
            pending = pending[-512:]
        time.sleep(0.02)


if __name__ == "__main__":
    raise SystemExit(main())
