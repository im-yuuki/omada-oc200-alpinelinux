#!/usr/bin/env python3
"""Small local helper for the persistent OC200 RFC2217 monitor."""
import argparse
import os
import socket
import sys

BASE = os.path.dirname(os.path.abspath(__file__))
SOCKET_PATH = os.path.join(BASE, "oc200-rfc2217-control.sock")


def send(payload: bytes) -> None:
    sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    sock.settimeout(5)
    try:
        sock.connect(SOCKET_PATH)
        sock.sendall(payload)
        sock.shutdown(socket.SHUT_WR)
    finally:
        sock.close()


def main() -> int:
    parser = argparse.ArgumentParser(description="Send bytes to the OC200 serial console")
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument("--text", help="send text followed by CR")
    group.add_argument("--bytes", help="send hexadecimal bytes, e.g. 03 or 1b 5b 32 30 7e")
    group.add_argument("--cr", action="store_true", help="send carriage return")
    group.add_argument("--ctrl-c", action="store_true", help="send Ctrl-C")
    group.add_argument("--ctrl-b", action="store_true", help="send Ctrl-B")
    args = parser.parse_args()

    if args.text is not None:
        payload = args.text.encode() + b"\r"
    elif args.bytes is not None:
        try:
            payload = bytes.fromhex(args.bytes)
        except ValueError as exc:
            parser.error(f"invalid hex bytes: {exc}")
    elif args.cr:
        payload = b"\r"
    elif args.ctrl_c:
        payload = b"\x03"
    else:
        payload = b"\x02"

    if not os.path.exists(SOCKET_PATH):
        print(f"monitor socket not found: {SOCKET_PATH}", file=sys.stderr)
        return 1
    send(payload)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
