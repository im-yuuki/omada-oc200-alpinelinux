#!/usr/bin/env python3
import argparse
import socket
from pathlib import Path

p = argparse.ArgumentParser()
p.add_argument('output', type=Path)
p.add_argument('port', type=int)
p.add_argument('size', type=int)
p.add_argument('--timeout', type=float, default=2.0)
a = p.parse_args()
s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
s.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 8 * 1024 * 1024)
s.bind(('0.0.0.0', a.port))
s.settimeout(5.0)
received = 0
packets = 0
with a.output.open('wb') as f:
    while received < a.size:
        try:
            data, peer = s.recvfrom(65535)
        except socket.timeout:
            if packets:
                break
            continue
        if not data:
            continue
        f.write(data)
        received += len(data)
        packets += 1
        if packets % 256 == 0 or received >= a.size:
            print(f'{received}/{a.size} bytes ({packets} datagrams)', flush=True)
print(f'finished: {received}/{a.size} bytes, {packets} datagrams')
