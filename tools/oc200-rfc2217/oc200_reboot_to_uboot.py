#!/usr/bin/env python3
import os
import socket
import time
from pathlib import Path

BASE = Path(__file__).resolve().parent
SOCKET_PATH = BASE / "oc200-rfc2217-control.sock"
CONSOLE_PATH = BASE / "oc200-console.bin"
USERNAME = b"oc200admin"
PASSWORD = b"Oc200!Rfc2217-2026"


def send(payload: bytes) -> None:
    with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as sock:
        sock.settimeout(5)
        sock.connect(str(SOCKET_PATH))
        sock.sendall(payload)
        sock.shutdown(socket.SHUT_WR)


def wait_for(needles, offset, timeout):
    deadline = time.monotonic() + timeout
    data = bytearray()
    while time.monotonic() < deadline:
        with CONSOLE_PATH.open("rb") as stream:
            stream.seek(offset)
            chunk = stream.read()
            offset = stream.tell()
        if chunk:
            data.extend(chunk)
            for needle in needles:
                if needle in data:
                    return needle, offset, bytes(data)
        time.sleep(0.03)
    raise TimeoutError(f"timed out waiting for {needles!r}; got {bytes(data)[-800:]!r}")


def command(command, expected, timeout=8):
    before = CONSOLE_PATH.stat().st_size
    send(command + b"\r")
    return wait_for(expected, before, timeout)


def main():
    before = CONSOLE_PATH.stat().st_size
    send(b"\r")
    prompt, offset, output = wait_for((b"Username:", b">", b"#"), before, 8)
    if prompt == b"Username:":
        send(USERNAME + b"\r")
        _, offset, _ = wait_for((b"Password:",), offset, 5)
        send(PASSWORD + b"\r")
        _, offset, _ = wait_for((b">",), offset, 8)
    if prompt != b"#":
        send(b"enable\r")
        _, offset, _ = wait_for((b"#",), offset, 5)
    send(b"reboot\r")
    _, offset, _ = wait_for((b"Continue? (Y/N):",), offset, 8)
    send(b"y\r")
    _, offset, _ = wait_for((b"Press CTRL+B",), offset, 30)
    send(b"\x02")
    _, _, output = wait_for((b"Marvell>>",), offset, 8)
    print(output.decode("latin1", "replace"))


if __name__ == "__main__":
    main()
