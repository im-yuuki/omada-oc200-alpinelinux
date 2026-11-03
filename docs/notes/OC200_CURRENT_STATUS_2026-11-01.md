# OC200 current status — 2026-11-01

> Dated status snapshot taken before the later eMMC install and readback. Some
> early statements below (including “no eMMC write”) were superseded later the
> same day. See `OC200_EMMC_ALPINE_INSTALL_PLAN.md` for the installation
> timeline and workspace-root `BETA_BUILD.md` for the current handoff.

## Executive result

The OC200 now boots a custom Linux 6.18.59 kernel and Alpine Linux 3.24 userspace entirely through the non-persistent U-Boot/TFTP path.

The tested runtime is:

- Linux 6.18.59 custom OC200 kernel
- Alpine Linux 3.24 aarch64
- AR8236 DSA Ethernet driver
- `lan0` link at 100 Mbps/full duplex
- DHCP address `192.168.12.26/24`
- SSH server listening on TCP port 22
- root login password `alpine`
- Bash, GNU coreutils, iperf3 and network utilities available
- SquashFS lower filesystem with RAM-backed OverlayFS runtime layer

No eMMC or SPI-NOR write has been performed.

## Verified boot path

At the U-Boot prompt, the current manual boot is:

```text
setenv ipaddr 192.168.12.240
setenv serverip 10.255.255.1
setenv gatewayip 192.168.12.1
setenv netmask 255.255.255.0
ping 10.255.255.1

tftpboot 0x05000000 oc200_test/oc200-6.18.59-alpine-full-usb-Image
tftpboot 0x04f00000 oc200_test/oc200-6.18.59-final.dtb
tftpboot 0x20000000 oc200_test/oc200-squashfs-initramfs.uImage
setenv initrd_size ${filesize}
setenv bootargs console=ttyMV0,115200 earlycon=ar3700_uart,0xd0012000 rdinit=/init loglevel=7
booti 0x05000000 0x20000000:${initrd_size} 0x04f00000
```

Do not run `saveenv`, `mmc write`, `sf write`, or `sf erase`.

The TFTP compatibility server is running on:

```text
10.255.255.1:1069
```

UDP/69 forwarding is active for the board's U-Boot TFTP client.

## Current TFTP artifacts

Directory:

```text
/var/lib/tftpboot/oc200_test/
```

| Artifact | Size | SHA-256 |
|---|---:|---|
| `oc200-6.18.59-alpine-full-usb-Image` | 40,327,680 | `97ce21c126ec6e4a312674238ff27f7b8bc0154d5e4886f69266f24740eda62e` |
| `oc200-6.18.59-final.dtb` | 12,339 | `7b025fe2118929aaa220b399257b469a0512b0071432abe990bdd5f94b8e9520` |
| `oc200-squashfs-initramfs.uImage` | 11,732,990 | `c6338eeaabd4a0314bc6334541040b2430848046c632ab755c43e8f64aa69e93` |
| `oc200-alpine-3.24.3-ssh.squashfs` | 10,915,840 | `92057995d63c8265a0085a1c5c11045a9fa25dedf414c69de115786a599f7960` |

The initramfs embeds the same SquashFS payload. It creates:

```text
SquashFS lowerdir: read-only Alpine root
RAM tmpfs:         upper/work layers
OverlayFS:         writable root view
```

All runtime changes disappear on reboot.

## Confirmed runtime output

Serial login succeeded with:

```text
login: root
password: alpine
```

Network and tools were verified on the board:

```text
lan0@eth0        UP             192.168.12.26/24
default via 192.168.12.1 dev lan0 metric 203
carrier=1

/bin/bash
/bin/coreutils
/usr/bin/iperf3
/usr/sbin/sshd

GNU bash, version 5.3.9(1)-release (aarch64-alpine-linux-musl)
coreutils (GNU coreutils) 9.11
iperf 3.20
LISTEN 0 128 0.0.0.0:22 0.0.0.0:*
```

The host-side SSH attempt to `192.168.12.26` timed out because the board is behind the current NAT/topology. The daemon was confirmed listening from the serial shell.

## Kernel configuration relevant to this runtime

The built kernel includes:

```text
CONFIG_BLOCK=y
CONFIG_MMC_BLOCK=y
CONFIG_MMC_BLOCK_MINORS=32
CONFIG_PARTITION_ADVANCED=y
CONFIG_MSDOS_PARTITION=y
CONFIG_TMPFS=y
CONFIG_SHMEM=y
CONFIG_OVERLAY_FS=y
CONFIG_DEVTMPFS=y
CONFIG_DEVTMPFS_MOUNT=y
CONFIG_UNIX98_PTYS=y
CONFIG_SQUASHFS=y
CONFIG_SQUASHFS_XZ=y
CONFIG_BLK_DEV_LOOP=y
```

The kernel also contains the OC200-specific AR8236 DSA driver and the OC200 DT topology/eMMC settings.

The full Alpine-baseline kernel was built successfully with `-j$(nproc)` (`nproc=80`), exit status 0, approximately 43.56 seconds wall time, and 918,724 KB peak RSS. Its release is `6.18.59` with no `-oc200` suffix.

## Storage facts

Read-only hardware information:

```text
SPI NOR: gd25lq80, 1 MiB
eMMC:   4FTE4R, approximately 3.6 GiB
DRAM:   1 GiB
```

Observed eMMC user partition map:

```text
p1: start 66560,   size 1047552 sectors
p2: start 1114112, size 1024 sectors
p3: start 1115136, size 2048 sectors
p4: start 1117184, size 7200768 sectors
```

`mmcblk0boot0` and `mmcblk0boot1` are 4 MiB eMMC boot areas; they have not been written.

No valid full SPI-NOR/eMMC backup exists yet. Placeholder backup files are zero bytes. Do not flash until a reliable external backup path is established.

## Boot verification/signature findings

The captured U-Boot environment uses raw MMC reads and `booti`:

```text
loadfdt=mmc dev 0; mmc read $fdt_addr 0x380 0x80
loadkernel=mmc dev 0; mmc read $kernel_addr 0x400 0x8000
bootcmd=run set_bootargs; run loadfdt; run loadkernel; booti $kernel_addr - $fdt_addr
```

No visible signature verification command appears in this boot path. Manual `tftpboot` plus `booti` has successfully booted unsigned custom kernels and DTBs.

The `TP-LINK_SMB_GATEWAY_RSA2048_PSS` challenge was observed in the vendor userspace `debug` command. It is not evidence that manual U-Boot `booti` enforces a kernel signature.

A raw NOR dump is still required before analyzing vendor recovery/update verification. No bootloader patch has been attempted.

## Current limitations and next work

1. The runtime is a TFTP/RAM test image, not a persistent installation.
2. Host-to-board SSH requires the user-side NAT/forwarding path to expose `192.168.12.26:22`, or a board-side reverse tunnel.
3. SSH must be re-tested after the current rebuilt OverlayFS image is booted; the earlier SSH reset was caused by the unsupported `UsePAM` directive and by stale embedded rootfs payloads.
4. An eMMC installation should use a near-stock Alpine ext4 rootfs rather than this custom SquashFS wrapper. That work is intentionally deferred.
5. The current password is deliberately weak and must not be used on an untrusted network.

## NIC performance investigation

The AR8236 is a 10/100 switch, so the approximately 94 Mbps reverse iperf3
result is near the hardware ceiling. The forward board-to-host test is
asymmetric: approximately 55–61 Mbps with roughly 200 TCP retries per second.
Runtime counters show no mvneta CRC, collision, overrun or MAC errors. The
current priority hypothesis is a pause/flow-control mismatch between the
AR8236 user port and the fixed 100 Mbps CPU port, followed by possible QCA
DSA-tag/offload interaction. Detailed evidence and A/B tests are documented
in `research/OC200_AR8236_IPERF_INVESTIGATION.md`.

## eMMC read-only dump

A direct SSH session to `root@192.168.12.26` was established while the board was running the RAM/TFTP system. Read-only `dd` streams were collected into:

```text
<archive>/research/backups/oc200-emmc-2026-11-01/
```

The directory contains the user-area dump plus the two eMMC boot-area reads, SHA-256 metadata, and a README. The actual device-side `dd` streams reached EOF earlier than the stale sysfs logical sizes: the user area is 3,907,452,928 bytes, while sysfs reported 3,909,091,328 bytes; each boot-area stream is also shorter than its 4 MiB sysfs size. The files preserve the actual returned bytes and are not padded. RPMB was intentionally not accessed. No board-side write command was used.
