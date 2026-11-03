# OC200 U-Boot read-only dump

Collected: 2026-10-31

The board was rebooted through the userspace CLI, the reboot confirmation was accepted, and Ctrl-B was sent on the U-Boot one-second countdown. The board is currently at `Marvell>>`.

## Captured command outputs

```text
uboot-printenv.txt
uboot-mmc-list.txt
uboot-mmc-info.txt
uboot-part-list.txt
uboot-sf-probe.txt
uboot-bdinfo.txt
uboot-version.txt
```

## U-Boot facts

```text
U-Boot 2017.03-armada-17.10.1 (Jan 25 2024 - 14:42:29 +0800)
Model: Marvell Armada 3720 ECK Board
DRAM: 1 GiB
MMC: sdhci@d8000: 0
SPI NOR: gd25lq80, 1 MiB, 256-byte page, 4 KiB erase
```

The stored environment has a bad CRC and U-Boot uses built-in defaults. The dangerous default write commands are not evidence of the real layout:

```text
burnfdt   -> mmc write block 0x380
burnkernel -> mmc write block 0x400
burnrootfs -> mmc write block 0x10400
```

No write command was issued.

## eMMC identity

```text
Manufacturer ID: 15
OEM: 100
Name: 4FTE4
Tran Speed: 52000000
MMC version 5.1
Capacity: 3.6 GiB
Bus Width: 8-bit
Erase Group Size: 512 KiB
HC WP Group Size: 8 MiB
Boot Capacity: 4 MiB ENH
RPMB Capacity: 512 KiB ENH
```

## DOS/MBR partition map

```text
Part  Start Sector  Num Sectors
  1       66560       1047552
  2     1114112          1024
  3     1115136          2048
  4     1117184       7200768
```

Sector size is 512 bytes. Converted ranges:

```text
p1: sectors 66560..1114111, 536346624 bytes (~511.4 MiB)
p2: sectors 1114112..1115135, 524288 bytes
p3: sectors 1115136..1117183, 1048576 bytes
p4: sectors 1117184..8317951, 3686793216 bytes (~3.43 GiB)
```

The final partition ends at sector 8317951, total addressable range 8317952 sectors / 4258791424 bytes (~3.97 GiB nominal user area), while U-Boot reports 3.6 GiB due to capacity conventions.

Linux vendor log independently reports p1-p4 and warns that p4 extends beyond EOD/truncates. Reconcile this using Linux `fdisk`/`parted` and `mmc extcsd` before any repartition/write.

## Network/U-Boot

U-Boot factory MAC:

```text
1C-3B-F3-77-31-6F
```

Default network variables are stale/generic because the environment CRC is bad:

```text
ipaddr=192.168.0.253
serverip=192.168.0.101
gatewayip=192.168.0.1
netmask=255.255.255.0
```

For the user's network, future TFTP commands should set values explicitly, preferably:

```text
setenv ipaddr 192.168.12.240
setenv serverip 10.255.255.1
setenv gatewayip 192.168.12.1
setenv netmask 255.255.255.0
```

Do not call `saveenv` until NOR has been backed up and boot has been tested; `saveenv` writes the SPI NOR environment area.

## Current state

At the time of this 2026-10-31 capture, the board remained at `Marvell>>`. No
flash or eMMC write was performed during that dump. A later SPI-NOR
default-environment intervention was performed on 2026-11-01; see
`docs/notes/OC200_BOOTLOADER_INTERVENTION_2026-11-01.md` for the changed state and
verification. The RFC2217 monitor note above is historical, not a statement of
the board's current connection state.
