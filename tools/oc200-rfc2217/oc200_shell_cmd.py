#!/usr/bin/env python3
import socket, sys, time
from pathlib import Path
BASE=Path(__file__).resolve().parent
SOCKET_PATH=str(BASE/'oc200-rfc2217-control.sock')
CONSOLE_PATH=BASE/'oc200-console.bin'

def send(data):
    s=socket.socket(socket.AF_UNIX,socket.SOCK_STREAM); s.settimeout(5)
    try:
        s.connect(SOCKET_PATH); s.sendall(data); s.shutdown(socket.SHUT_WR)
    finally: s.close()

def main():
    if len(sys.argv)<2:
        print('usage: oc200_shell_cmd.py COMMAND [TIMEOUT]',file=sys.stderr); return 2
    timeout=12.0
    if sys.argv[-1].replace('.','',1).isdigit() and len(sys.argv)>2:
        timeout=float(sys.argv[-1]); command=' '.join(sys.argv[1:-1])
    else: command=' '.join(sys.argv[1:])
    before=CONSOLE_PATH.stat().st_size
    send(command.encode()+b'\r')
    deadline=time.monotonic()+timeout; offset=before; out=bytearray()
    while time.monotonic()<deadline:
        with CONSOLE_PATH.open('rb') as f:
            f.seek(offset); chunk=f.read(); offset=f.tell()
        if chunk:
            out.extend(chunk)
            if b'~ # ' in out or b'~ #\r' in out or b'# ' in out:
                break
        time.sleep(.03)
    sys.stdout.buffer.write(bytes(out))
    return 0
if __name__=='__main__': raise SystemExit(main())
