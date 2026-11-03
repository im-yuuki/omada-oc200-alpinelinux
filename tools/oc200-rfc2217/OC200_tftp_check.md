# OC200 U-Boot TFTP check

Notes on the OC200 U-Boot TFTP client and what a standard tftpd does
differently, motivated by TFTP transfers failing with stock `tftpd-hpa`
and working with a custom server harness.

## Symptom

`Marvell>>` `tftpboot` invoked a transfer that stayed at `T #` retries and
never completed with `tftpd-hpa` (v5.x) on the lab Linux host, while the
same file transferred with `curl tftp://` and with the Python server below.

An upstream TFPT client (old Marvell/U-Boot era) was implemented before RFC
2348 (`blksize`) was widely honoured, and retries on the data channel with
standard servers. Behavior changed once the compat server below was used.

## Compatibility findings

The Python compat server (`tftp_compat_server.py`, same repository)
implements the following deliberately non-standard behavior to satisfy the
OC200 U-Boot bootloader client:

- Replies on **UDP port 69 for every packet**, not from an ephemeral port
  selected per-session as RFC 1350 defines. The old client apparently
  locks onto the well-known port after the first RRQ and ignores data from
  other source ports.
- Honours **blksize** negotiation up to large sizes but falls back to 512 if
  the client's block-number state machine gets confused by an OACK; the
  client also seems to always start its block numbering at **1 for both the
  data stream and the ACK stream**, which transparently works with this
  server.
- Does **not** implement multicast or RFC 7440 windowsize; the client does
  not use them anyway.
- Waits for the client to retransmit rather than pre-emptively re-sending —
  packet pacing matters more than faster retry.

## What this implies

- With `tftpd-hpa`, transfers of small files may "appear" to work by luck
  when the ephemeral port happens to be reused, which masks the problem.
- On macOS/Windows hosts where binding UDP/69 is awkward alongside other
  services, run the compat server with an explicit bind address/interface.
- Transfer rate against the old client is inherently low because it uses
  512-byte blocks and a single outstanding packet; moving ~12 MiB takes
  notable minutes. Do not "fix" this by raising blksize; the client does
  not benefit.

## Console observation

From `oc200-console-transcript.txt`, the client (U-Boot 2013.01-v1.0,
8688K RAM) announces itself as:

```text
 Using egiga0 device
 TFTP from server 10.255.255.1; our IP address is 192.168.12.240
 Filename 'oc200_test/alpine-3.24.3-oc200-initramfs.uImage'.
 Load address: 0x20000000
 Loading: ################################################################
          512 KiB/s
 done
 Bytes transferred = 12241408 (badbf00 hex)
```

at 512 KiB/s with the compat server, versus `T #` loops with `tftpd-hpa`.
