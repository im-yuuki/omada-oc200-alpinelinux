# OC200 AR8236 / iperf3 investigation

Status: runtime A/B tests completed; an explicit flow-bit programming change is
in the current kernel worktree. Results do not confirm pause mismatch as the
cause; validate the driver change with the same forward/reverse test after boot.

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
research/kernel/linux-6.18.59/drivers/net/dsa/qca/ar8236.c
```

The setup path enables flow-related state on user ports, while the fixed CPU
port is written without pause bits. The phylink callback does not fully
program the AR8236 port status flow bits from `tx_pause`/`rx_pause`.

This can produce exactly the observed pattern: host-to-board traffic is near
line rate, while board-to-host traffic sees congestion and TCP retransmits.

### 3. QCA tagger is modern DSA code, not the old 4.4 implementation

The active tagger is:

```text
research/kernel/linux-6.18.59/net/dsa/tag_qca.c
```

It uses the modern DSA etype-header helpers. The old vendor implementation is
preserved for comparison in:

```text
research/ar8236-port/tag_qca-4.4.c
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
