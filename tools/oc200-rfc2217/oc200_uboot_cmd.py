#!/usr/bin/env python3
import os
import socket
import sys
import time
from pathlib import Path

BASE = Path(__file__).resolve().parent
SOCKET_PATH = str(BASE / "oc200-rfc2217-control.sock")
CONSOLE_PATH = BASE / "oc200-console.bin"


def send(data):
    sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    sock.settimeout(5)
    try:
        sock.connect(SOCKET_PATH)
        sock.sendall(data)
        sock.shutdown(socket.SHUT_WR)
    finally:
        sock.close()


def main():
    if len(sys.argv) < 2:
        print("usage: oc200_uboot_cmd.py COMMAND [TIMEOUT]", file=sys.stderr)
        return 2
    command = " ".join(sys.argv[1:-1]) if len(sys.argv) > 2 and sys.argv[-1].replace('.', '', 1).isdigit() else " ".join(sys.argv[1:])
    timeout = float(sys.argv[-1]) if command != " ".join(sys.argv[1:]) else 12.0
    before = CONSOLE_PATH.stat().st_size
    send(command.encode() + b"\r")
    deadline = time.monotonic() + timeout
    offset = before
    output = bytearray()
    while time.monotonic() < deadline:
        with CONSOLE_PATH.open("rb") as stream:
            stream.seek(offset)
            chunk = stream.read()
            offset = stream.tell()
        if chunk:
            output.extend(chunk)
            if b"Marvell>>" in output:
                break
        time.sleep(0.02)
    text = bytes(output).decode("latin1", errors="replace")
    sys.stdout.write(text)
    return 0 if b"Marvell>>" in output else 1


if __name__ == "__main__":
    raise SystemExit(main())
