# OC200 RFC2217 workspace helpers

Board console is reachable only through the forwarded RFC2217 endpoint:

```text
192.168.12.5:34567
```

The board is behind NAT. Do not expect direct inbound connections to its userspace IP. The current userspace address `192.168.12.26` is DHCP-assigned. The planned static address `192.168.12.240` is outside the DHCP pool and should be preferred for future TFTP boot configuration; `.26` remains usable while its DHCP lease is active. The persistent local monitor reads the forwarded serial stream and reconnects if the forwarding drops.

## Files

- `oc200_rfc2217_monitor.py` — persistent RFC2217 monitor with reconnect and capture.
- `oc200_serialctl.py` — local helper for sending console input through the monitor; no script needs to be pasted into the board.
- `oc200-console-wire.bin` — raw Telnet/RFC2217 bytes.
- `oc200-console.bin` — decoded serial payload.
- `oc200-console-transcript.txt` — text snapshot of the decoded capture.
- `oc200-rfc2217.events.log` — monitor events.
- `OC200_console_readonly_findings.md` — read-only findings.
- `OC200_credentials.txt` — plaintext credential; mode 600.

## Helper examples

```bash
./oc200_serialctl.py --cr
./oc200_serialctl.py --text 'show system-info'
./oc200_serialctl.py --ctrl-c
./oc200_serialctl.py --bytes 'ff fa 2c 05 05 ff f0'
```

The monitor's Unix control socket is created in this directory while it is running. The monitor currently runs detached; check the PID file and process list before starting another instance.

No flash writes have been performed. Do not issue `reboot`, `saveenv`, `mmc write`, `sf write`, or related destructive commands without an explicit step-by-step decision and backups.
