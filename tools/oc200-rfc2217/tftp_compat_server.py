#!/usr/bin/env python3
"""Minimal option-free TFTP server for old U-Boot clients behind a routed UDP path."""
import argparse
import os
import socket
import struct
import sys
import time
from pathlib import Path

RRQ = 1
DATA = 3
ACK = 4
ERROR = 5
BLOCK_SIZE = 512


def packet(op, block, payload=b""):
    return struct.pack("!HH", op, block) + payload


def run(bind, port, root):
    root = root.resolve()
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock.bind((bind, port))
    sock.settimeout(1.0)
    print(f"listening on {bind}:{port}, root={root}", flush=True)
    while True:
        try:
            data, peer = sock.recvfrom(2048)
        except socket.timeout:
            continue
        if len(data) < 4 or struct.unpack("!H", data[:2])[0] != RRQ:
            continue
        fields = data[2:].split(b"\0")
        if len(fields) < 2:
            continue
        name = fields[0].decode("utf-8", "replace")
        clean = name.strip(" \t\r\n")
        if clean != name:
            print(f"RRQ note: name has hidden characters {name!r}; "
                  f"using {clean!r}", flush=True)
            name = clean
        path = (root / name).resolve()
        reason = None
        try:
            path.relative_to(root)
            content = path.read_bytes()
        except (OSError, ValueError) as exc:
            if isinstance(exc, ValueError):
                reason = "target is outside tftp-root (symlinked file?)"
            elif isinstance(exc, FileNotFoundError):
                reason = "no such file in tftp-root (tftp-root has: %s)" % (
                    ", ".join(sorted(p.name for p in root.iterdir()))[:200]
                    if root.is_dir() else "(root is not a directory)")
            else:
                reason = f"unreadable ({exc})"
            print(f"RRQ refused {peer[0]}:{peer[1]} {name!r}: {reason}", flush=True)
            sock.sendto(packet(ERROR, 1, b"File not found\0"), peer)
            continue
        print(f"RRQ {peer[0]}:{peer[1]} {name} ({len(content)} bytes)", flush=True)
        offset = 0
        block = 1
        while True:
            chunk = content[offset:offset + BLOCK_SIZE]
            msg = packet(DATA, block, chunk)
            for _ in range(8):
                sock.sendto(msg, peer)
                deadline = time.monotonic() + 2.0
                while time.monotonic() < deadline:
                    try:
                        reply, reply_peer = sock.recvfrom(2048)
                    except socket.timeout:
                        continue
                    if reply_peer != peer or len(reply) < 4:
                        continue
                    op, ack_block = struct.unpack("!HH", reply[:4])
                    if op == ACK and ack_block == block:
                        break
                else:
                    continue
                break
            else:
                print(f"transfer failed {name}: block={block}", flush=True)
                break
            offset += len(chunk)
            if len(chunk) < BLOCK_SIZE:
                print(f"complete {name}", flush=True)
                break
            block = (block + 1) & 0xffff


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--bind", default="10.255.255.1")
    parser.add_argument("--port", type=int, default=1069)
    parser.add_argument("--root", type=Path, default=Path("/var/lib/tftpboot"))
    args = parser.parse_args()
    run(args.bind, args.port, args.root)


if __name__ == "__main__":
    main()
