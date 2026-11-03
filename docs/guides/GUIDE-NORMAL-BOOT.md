# OC200 Beta 2 — normal boot guide

This guide assumes the Beta 2 image
(`oc200-alpine-3.24.3-full-emmc-image.img`, SHA-256
`ef0540f88a01bea6f4cceedc62f00b630b0d26915602708b13fd972e43a83c69`)
has been installed to the board's eMMC user area. If it has not, use
`GUIDE-RECOVERY.md` first.

> **Board update (2026-11-01):** the OC200's SPI-NOR compiled default
> environment was patched separately to autoboot this eMMC layout, retaining
> the vendor `httpd` fallback after a failed `booti`. Full-NOR readback matched
> the candidate before reboot; the user reported the reboot succeeded. See
> `research/OC200_BOOTLOADER_INTERVENTION_2026-11-01.md`. The manual sequence
> below remains a recovery/diagnostic option; do not treat the earlier
> “never touch SPI NOR” advice as a description of the board's current state.

## What is installed

| Region | eMMC LBA (512 B) | Content |
|---|---|---|
| MBR | 0 | partition table; p1 spans 0x20000 sectors (64 MiB), p2 starts at 0x40000 |
| DTB | 0x380 (896) | `oc200-patched.dtb` (12,345 B) |
| Kernel | 0x400 (1024) | `oc200-linux-6.18.59-Image` (42,789,376 B) |
| p1 (EROFS, `rootfs` label) | 0x20000 (131072) | read-only Alpine 3.24.3 rootfs |
| p2 (F2FS, `userdata` label) | 0x40000 (262144) | writable userdata / overlay upper + work |

Verify binary payloads against `SHA256SUMS.txt` and consult
`build-manifest.json` for the eMMC image layout.

## Serial console (required for U-Boot)

- 115200 baud, 8N1, no flow control. Stop autoboot with **Ctrl-B** at
  `Marvell>>`.
- The board UART runs at 2.5 V logic. Do not connect a 5 V UART signal
  directly; use a proper level shifter (the original lab setup used a
  TXS0108E with VCCA = 2.5 V and common ground). Verify TX/RX/GND before
  wiring.

## Booting from eMMC

The stored persistent U-Boot environment had a bad CRC and falls back to
compiled defaults. On 2026-11-01 those compiled defaults were patched in SPI
NOR for normal eMMC autoboot. The sequence below remains available for manual
boot/diagnosis from the U-Boot prompt:

```text
setenv loadkernel 'mmc dev 0; mmc read ${kernel_addr} 0x400 0x1fc00'
setenv loadfdt 'mmc dev 0; mmc read ${fdt_addr} 0x380 0x19'
setenv bootargs 'console=ttyMV0,115200 earlycon=ar3700_uart,0xd0012000 root=/dev/mmcblk0p1 rootwait rw'
run loadfdt
run loadkernel
booti ${kernel_addr} - ${fdt_addr}
```

Notes:

- The kernel read window `0x1fc00` sectors is required: the Beta 2 kernel is
  ~42 MB and the vendor's default small window truncates it.
- `setenv` changes live in RAM only. Never run `mmc write` or `sf write` from
  this prompt.
- If your build's `env help` lists an `env save` subcommand, persistent
  environment saving is possible, but it writes SPI NOR and is not needed for
  the patched compiled defaults. Do not save or alter the environment without
  a separate, verified recovery plan.

## After boot

- `rootfs` (p1) is read-only EROFS; `/etc` and userdata are an overlay on p2
  (F2FS). Writes to `/etc` are persistent across reboot.
- The board takes DHCP on `lan0` (DSA ports `swp1`/`swp2` are the switch
  data ports). Log in over serial as `root` / `alpine`, or SSH
  (`ssh root@<lease-ip>`, password `alpine`) on port 22.
- Timezone is UTC; chrony syncs time.

## Quick acceptance checks

```sh
uname -r                     # 6.18.59
cat /proc/cmdline            # root=/dev/mmcblk0p1
grep -E 'erofs|f2fs' /proc/mounts
blkid /dev/mmcblk0p1 /dev/mmcblk0p2   # labels rootfs / userdata
apk audit >/dev/null && echo audit-ok
touch /etc/.probe && ls /etc/.probe && rm /etc/.probe   # writable, persists
rc-status -a                 # networking, sshd, chronyd running
ip -br addr                  # swp1/swp2 DHCP
free -m                      # ~512 MiB zram swap present, no other swap
```

Expect the Cloud LED to show disk activity; the Power LED stays unchanged.
Beta 2 also includes the GPIO-key driver and built-in Docker bridge/NAT
networking. After boot, `docker info` and `docker ps` should reach the daemon
once the Docker service is started.
Reboot once and confirm `/etc` changes survived.

## Known Beta limitations

- The AR8236 flow-control change is included but its throughput benefit is
  unproven. Switch ports are 10/100 Mbps; ~94–95 Mbps reverse iperf3 is near
  line rate. Do not install iperf3 persistently.
- The compiled defaults were patched for autoboot, but the stored environment
  still has a bad CRC. Avoid `saveenv`/`env save`; the patch is in SPI NOR and
  must be handled only with the intervention record's precautions.
- Never touch `mmcblk0boot0`, `mmcblk0boot1`, or `mmcblk0rpmb`. Do not make
  further SPI-NOR changes without a verified backup and recovery plan.

If the board fails to boot from eMMC, use `GUIDE-RECOVERY.md` to bring it
back via RAM recovery.
