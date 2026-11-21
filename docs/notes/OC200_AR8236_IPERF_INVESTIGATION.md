# OC200 AR8236 / iperf3 investigation

Status: forward TCP retransmissions and asymmetric UDP loss reproduced on
Beta 2 build #8 (2026-11-21). Earlier tests did not establish that the flow-bit
change fixed forward throughput or that pause is the cause. See the latest
runtime results below. The source bundle now includes an MIB diagnostics
patch; it is not in the published build #8 image.

## Symptom

The user's network uplink and other devices operate at 1 Gbps. The OC200 has an
AR8236 10/100 switch, so its physical ceiling is 100 Mbps. Observed tests:

```text
board -> 192.168.12.5:5201: approximately 55 Mbps, roughly 200 TCP retries/s
reverse (-R): approximately 94 Mbps, roughly 10 TCP retries/s
```

The reverse result is close to the expected 100 Mbps wire-rate ceiling. The
problem is therefore asymmetric packet loss or backpressure in the OC200
transmit path, not the external gigabit network being limited to 100 Mbps.

## Reproduced board-side evidence

The direct SSH/serial test environment reported:

```text
lan0@eth0        UP             192.168.12.26/24
default via 192.168.12.1 dev lan0 metric 203
carrier=1
```

`lan0`:

```text
Supported: 10/100, full and half duplex
Speed:     100Mb/s
Duplex:    Full
Autoneg:   on
Link:      detected
```

`eth0`, the Armada mvneta CPU-side conduit:

```text
Speed:     100Mb/s
Duplex:    Full
Link:      detected
```

The board-side iperf3 run to `192.168.12.5` reproduced the asymmetry:

```text
duration:       10 seconds
direction:      board -> host
throughput:     61.33 Mbps sender / 61.21 Mbps receiver
TCP retries:    1712
mean RTT:       1.172 ms
min/max RTT:    0.859 / 1.441 ms
```

The saved raw JSON is `/tmp/oc200-iperf-board-tx.txt` on the development host.

## Link and flow-control state

The current modern driver and DT configuration show a likely mismatch:

```text
lan0 local pause:       RX off, TX off
lan0 negotiated pause:  RX on,  TX on
eth0 negotiated pause:  RX off, TX off
```

The DT fixes the CPU-side RGMII link at 100 Mbps/full duplex and does not
advertise pause:

```dts
&eth0 {
        phy-mode = "rgmii";
        fixed-link {
                speed = <100>;
                full-duplex;
        };
};
```

The AR8236 setup also initializes user ports with:

```c
AR8236_PORT_STATUS_LINK_AUTO |
AR8236_PORT_STATUS_LINK_FLOW_EN |
AR8236_PORT_STATUS_TX_HALF_FLOW
```

The phylink link-up callback currently only ORs `LINK_FLOW_EN` when either
pause direction is negotiated. It does not explicitly clear stale
`TX_HALF_FLOW`, set/clear `TXFLOW` and `RXFLOW`, or synchronize the user-port
pause state with the fixed CPU port. This is the highest-priority driver
hypothesis.

## Hardware counters

The mvneta/eth0 counters during the test showed no physical-layer errors:

```text
bad_frames_received:       0
bad_crc_event:              0
rx_overrun:                 0
mac_receive_error:          0
collision:                  0
late_collision:             0
internal_mac_transmit_err:  0
```

Linux link statistics showed only a small number of drops compared with the
TCP retry count:

```text
lan0 RX: 159,869,510 bytes, 540,478 packets, 83 drops
lan0 TX: 1,658,120,336 bytes, 460,722 packets, 1 drop
eth0 RX/TX errors: 0
```

This does not prove that the switch did not drop frames: the AR8236 MIB
counters are not currently exposed through modern `ethtool -S`, so per-port
`RxOverflow`, `RxFcsErr`, `RxAlignErr`, `TxUnderRun`, and related counters are
not available in the standard runtime output.

## Driver review findings

### 1. CPU link is intentionally 100 Mbps

The AR8236 is a 10/100 switch. The 100 Mbps CPU link is expected and explains
the approximately 94 Mbps reverse ceiling. Do not attempt to make the port
advertise gigabit; the switch hardware cannot do that.

### 2. Pause state is not programmed symmetrically

Relevant code:

```text
release/beta2/kernel-repro/drivers/net/dsa/qca/ar8236.c
```

The setup path enables flow-related state on user ports, while the fixed CPU
port is written without pause bits. The phylink callback does not fully
program the AR8236 port status flow bits from `tx_pause`/`rx_pause`.

This can produce exactly the observed pattern: host-to-board traffic is near
line rate, while board-to-host traffic sees congestion and TCP retransmits.

### 3. QCA tagger is modern DSA code, not the old 4.4 implementation

The active tagger is:

```text
linux-6.18.59/net/dsa/tag_qca.c
```

It uses the modern DSA etype-header helpers. The old vendor implementation is
preserved for comparison in:

```text
research/ar8236/tag_qca-4.4.c
```

The tagger remains a secondary hypothesis. It should not be changed before
collecting switch MIB counters and running offload A/B tests.

### 4. mvneta has multiple queues and offloads enabled

The runtime reports eight TX queues and eight RX queues on the CPU conduit,
with TX checksum, TSO, GSO and GRO enabled where supported. The board has only
two CPUs and a 100 Mbps switch-side link. Queue scheduling/backpressure and
DSA tag placement should be tested before changing mvneta.

## Safe A/B tests for the next session

These tests are runtime-only and do not write flash:

### Flow-control test

```sh
ethtool -A lan0 rx off tx off
iperf3 -c 192.168.12.5 -t 10 -i 1
```

If retries drop significantly, the pause mismatch is confirmed as a major
factor.

### Runtime results — 2026-11-01

Using the same reachable iperf3 server at `192.168.12.5:5201` from the
persistent board at `192.168.12.28`:

| Test | Forward throughput / retries | Reverse throughput / retries |
| --- | --- | --- |
| Current negotiated pause | 55.3 Mbps / 1,106 | 95.3 Mbps / 0 |
| `ethtool -A lan0 autoneg off rx off tx off` | 54.1 Mbps / 1,095 | 95.3 Mbps / 55 |
| `ethtool -K eth0 tso off gso off gro off` | 52.3 Mbps / 1,062 | 95.3 Mbps / 60 |

Both runtime experiments were restored afterward (`lan0` pause autonegotiation
on, tx/rx off locally; `eth0` TSO/GSO/GRO on). Neither test materially
improved the asymmetric forward result. This weakens, but does not eliminate,
the pause/offload hypotheses. The current code change only clears stale
AR8236 directional/half-flow bits and sets `TXFLOW`/`RXFLOW` explicitly from
phylink's `tx_pause`/`rx_pause`; it does not force a pause policy on the fixed
CPU link. MIB exposure and tagger changes remain deferred.

### Runtime results — 2026-11-03 (after flow-bit change)

Board `192.168.12.6` to `192.168.12.3:5201`, 10 seconds, board-to-host
(forward), with the flow-bit driver change running:

| Test | Forward throughput / retries | Notes |
| --- | --- | --- |
| Before flow-bit change (2026-11-01, `192.168.12.5`) | 55.3 Mbps / 1,106 | baseline from the table above |
| After flow-bit change (2026-11-03, `192.168.12.3`) | 91.1 Mbps / 2,540 | sender 91.1 Mbps, receiver 91.0 Mbps; cwnd 8–36 KB |
| Reverse, same kernel (2026-11-03, `192.168.12.3 -R`) | 95.2 Mbps / 0 | sender 95.2 Mbps, receiver 93.9 Mbps; 10 s, one run |

Interpretation:

- Both directions now reach about 91–95 Mbps, close to the 100 Mbps switch
  ceiling. The throughput asymmetry is largely resolved.
- Reverse retries are 0, the same as before the change. Forward retries
  are still high: about 254/s, up from about 110/s before the change. The TX
  path from the board still drops or stalls frames, even though the throughput
  is higher.
- Forward and reverse were run on the same kernel and server in one session,
  so this pair is a like-for-like comparison. The earlier 2026-11-01 baseline
  used a different board IP (`.28`) and server (`.5`), so the before/after
  comparison is still approximate.

Next steps:

1. Record the kernel image hash on the board, so the result is tied to the
   driver version.
2. Investigate the remaining forward retries: CPU-port pause policy, AR8236
   MIB counters, and the tagger and mvneta items below.
3. Repeat the forward test a few times to see whether the retry count is stable.

### Runtime results — 2026-11-21 (Beta 2 build #8)

Measured without changing network settings. The board reported Linux
`6.18.59 #8`, and exact-length readbacks of the kernel and DTB slots matched
`release/beta2/build-manifest.json`. The route to `192.168.12.3` used `swp1`
directly; `swp1` was 100/full, MTU 1500, with RX/TX pause negotiated. The `eth0`
conduit was 100/full, MTU 1502, QCA tagging, no negotiated pause. Both live
fixed-link nodes had `speed` and `full-duplex` but no `pause` or `asym-pause`.

| TCP to `.3`, 8 s per run | Receiver Mbps | Sender retransmissions | Retransmissions per MiB sent |
| --- | ---: | ---: | ---: |
| Forward, two runs | 71.8–74.0 | 1,621–1,867 | 22.8–27.0 |
| Forward, `-b 40M`, two runs | 40.0–40.1 | 655–731 | 17.1–19.1 |
| Forward, `-M 1200`, two runs | 63.0–64.2 | 1,940–2,020 | 31.4–33.3 |
| Reverse | 93.3 | 74 | 0.8 |
| Bidirectional, board sends | 34.9 | 987 | 29.4 |
| Bidirectional, board receives | 90.0 | 76 | 0.9 |

Across these runs, the mvneta CRC, bad-frame, discard, overrun, and internal
TX-error counters did not increase. Neither did qdisc or softnet drops.
CPU0 handled the `eth0` IRQ and spent about 51% of the bidirectional run in
softirq; softnet `time_squeeze` increased by 86 in that run. This does not
establish that CPU load caused loss. Reducing offered load lowered but did
not eliminate retransmissions; reducing MSS did not help.

UDP to `.3:5201` failed during stream setup and did not yield a loss
measurement. A second endpoint on the same LAN, reached over Wi-Fi, also
reproduced TCP TX retransmissions (57.1 Mbps received, 1,252 retries in 5 s).
It reported forward UDP loss of 2.8–3.25% at 10 Mbps with 1,200-byte payloads,
3.04% at 2 Mbps with 256-byte payloads, and 3.14% at 40 Mbps with 1,200-byte
payloads. Reverse UDP at 40 Mbps reported no loss in one run. The small-packet
and repeated 10 Mbps runs did not raise the receiver's UDP checksum or socket
buffer error counters. The alternate path includes Wi-Fi, so these runs do
not localize the loss to AR8236. They show the broader TX symptom does not
require large frames, TCP, high offered load, or `.3` as the endpoint.

Next, repeat UDP using a wired peer (ideally direct, with a known-good cable
and each OC200 port in turn) and collect switch MIB deltas from CPU port 0 and
the active user port. The published build #8 has no DSA stats callbacks;
software `swp1` counters and mvneta RX counters do not cover switch port 0
ingress.

When revisiting pause, AR8236 port-status bit 12 selects PHY-negotiated
versus software-configured flow control; it is not a generic pause-enable
bit ([AR8236 datasheet, table 3-31](https://ickala.com/downloadscenter/8691-download/AR8236-AL1A.html)).
Choose one owner of the flow-control state rather than writing directional
bits and enabling PHY control in the same callback. For MIB collection,
serialize MDIO page selection and both halfword transfers, return register
data separately from errno, and accumulate the clear-on-read counters.

### MIB diagnostics in the source bundle

The patched driver exposes 39 AR8236 hardware counters per exposed DSA
port. `ethtool -S swp1` or `ethtool -S swp2` reports the corresponding
external switch port. `ethtool -S eth0` includes the CPU-port counters
prefixed with `s00_p00_`, alongside the existing mvneta counters.

Use before/after deltas per test, particularly CPU-port `RxFcsErr`,
`RxAlignErr`, `RxTooLong` and `RxOverFlow`, and external-port `RxPause`,
`TxPause` and `TxUnderRun`. The driver reads the clear-on-read registers on
demand and accumulates them in software. It does not trigger MIB autocast,
poll in the background, or flush counters. Take snapshots close to the
test so narrow hardware counters have no time to saturate or wrap.
If an MDIO read fails, the affected counter retains its cached total and
the driver logs a rate-limited warning; do not use that delta to rule out
loss. Runtime behavior is not verified on an OC200 with this patch.

### Offload test

```sh
ethtool -K eth0 tso off gso off gro off
iperf3 -c 192.168.12.5 -t 10 -i 1
```

If the fixed hardware features reject the command, record the exact output and
test whatever subset is changeable. A large improvement points to interaction
between segmentation/offload and the two-byte QCA DSA header.

### Before/after counters

```sh
ethtool -S eth0
ip -s link show lan0
cat /proc/net/netstat | grep TcpExt
```

The AR8236 driver should then gain a read-only MIB exposure path, preferably
through DSA `get_ethtool_stats`, for both user ports and the CPU port.

## Prioritized code changes to investigate later

1. Add explicit AR8236 port-status programming in `mac_link_up`:
   - clear `TX_HALF_FLOW` and stale `TXFLOW`/`RXFLOW` bits;
   - set `TXFLOW` and `RXFLOW` from `tx_pause`/`rx_pause`;
   - set/clear `LINK_FLOW_EN` consistently;
   - preserve speed/duplex/link state.
2. Decide whether CPU-port pause should be enabled on the fixed link, or force
   user-port pause off to match the fixed CPU link.
3. Expose AR8236 MIB counters through DSA ethtool statistics.
4. Only after that, investigate QCA tagger checksum/GSO behavior and mvneta
   queue/interrupt tuning.

No patch should be applied blindly: each change should be tested with the same
10-second forward and reverse iperf3 runs and the before/after counters.
