# OC200 Beta 2 — recovery and flash guide

RAM recovery boots a complete Alpine system from TFTP without touching the
eMMC, then flashes the Beta 2 image (or individual parts) to the eMMC **user
area**. Use it to install Beta 2 the first time, to replace a broken
kernel/rootfs, or to update parts.

## Files used (`release/beta2/`, payloads from the release assets)

The large payloads are published as `.zst` release assets. Decompress them
first (`zstd -d <file>.zst`) and check against `release/beta2/SHA256SUMS.txt`.

| File | Purpose | SHA-256 |
|---|---|---|
| `oc200-linux-6.18.59-Image` | kernel booted via TFTP | see `SHA256SUMS.txt` |
| `oc200-recovery-initramfs.uImage` | RAM rootfs **with WebUI + SSH**, kernel includes GPIO keys and built-in Docker bridge/NAT networking | see `SHA256SUMS.txt` |
| `oc200-patched.dtb` | current DTB, including GPIO LED pinctrl fix | see `SHA256SUMS.txt` |
| `oc200-alpine-3.24.3-full-emmc-image.img` | full eMMC image to install (3,907,452,928 B) | see `SHA256SUMS.txt` |

`alpine-3.24.3-rootfs.erofs` (52,043,776 B) and
`alpine-3.24.3-userdata.f2fs` (3,773,235,200 B) are the individual partitions; `mbr`/`dtb`/
`kernel` targets use the DTB/kernel files above plus the MBR from the full
image (`dd if=oc200-alpine-3.24.3-full-emmc-image.img of=mbr.bin bs=512 count=1` on
your PC).

## Step 1 — TFTP server

The board's old U-Boot client does **not** work with standard TFTP servers
(`tftpd-hpa`, `dnsmasq`); `tftpboot` may loop `T #` retries forever. Use the
bundled `tftp_compat_server.py` (requires root/sudo to bind port 69):

```sh
mkdir -p tftp-root
cp oc200-linux-6.18.59-Image \
   oc200-recovery-initramfs.uImage \
   oc200-patched.dtb tftp-root/
sudo python3 tftp_compat_server.py --bind <YOUR-LAN-IP> --port 69 \
  --root ./tftp-root
```

Transfer speed is limited by the old client (~512 KiB/s): budget a few
minutes per file.

## Step 2 — boot RAM recovery

Serial console 115200 8N1 (**Ctrl-B** to stop autoboot at `Marvell>>`;
board UART is 2.5 V logic — use a level shifter). Then, adjusting the board
IP/server IP to your LAN:

```text
setenv ipaddr 192.168.12.240
setenv serverip 10.255.255.1
setenv gatewayip 192.168.12.1
setenv netmask 255.255.255.0
ping ${serverip}
tftpboot 0x05000000 oc200-linux-6.18.59-Image
setenv initrd_size 0
tftpboot 0x20000000 oc200-recovery-initramfs.uImage
setenv initrd_size ${filesize}
tftpboot 0x04f00000 oc200-patched.dtb
setenv bootargs console=ttyMV0,115200 earlycon=ar3700_uart,0xd0012000 rdinit=/init loglevel=7
booti 0x05000000 0x20000000:${initrd_size} 0x04f00000
```

`rdinit=/init` is mandatory — the WebUI refuses to flash without it. RAM
recovery is non-persistent (this vendor build has no `saveenv` command at
all — env stays RAM-only; see `GUIDE-NORMAL-BOOT.md`).

The serial console then prints:

```text
OC200 WebUI: open http://<board-ip>/ , token: <16 hex characters>
```

The token is a one-time value for this boot; SSH (`root` / `alpine`) is also
available at the board's DHCP address.

## Step 3 — install Beta 2 (choose ONE option)

The write only ever touches the eMMC user area (`/dev/mmcblk0`). It never
touches `boot0`/`boot1`, RPMB, or SPI NOR. Flashing erases the current
userdata (p2) — back it up first if needed.

### Option A — WebUI (recommended)

1. From your PC (same LAN), open `http://<board-ip>/`.
2. Enter the token from the serial console.
3. Select target **`raw`** and choose
   `oc200-alpine-3.24.3-full-emmc-image.img`. The expected image SHA is
   built into the page and auto-fills.
4. Press **Flash**. The upload goes in 8 MiB chunks; each chunk is hashed on
   the board, and after the last chunk the whole user area is read back and
   compared. Wait for `verify OK`.
5. Press **Reboot**, then boot per `GUIDE-NORMAL-BOOT.md`.

**Important:** the full image contains the earlier DTB. Before normal boot,
flash `oc200-patched.dtb` separately using the `dtb` procedure below; this
version enables the GPIO-controlled Cloud/system LED.

Full image is ~3.64 GiB; upload and verify take a while over 100 Mbps —
leave the page open. You can **Abort** between chunks.

The WebUI intentionally does not hardcode release SHA-256 values. Enter the
expected value manually in the optional SHA field for strict matching, or
leave it empty to obtain a readback SHA and compare it externally with
`SHA256SUMS.txt`.

### Option B — SSH streaming dd (no web browser needed)

From a Linux/macOS PC with the image file, stream it directly — nothing is
stored on the board's RAM or /tmp:

```sh
TOKEN=...   # not needed over ssh
cat oc200-alpine-3.24.3-full-emmc-image.img | \
  ssh root@<board-ip> 'dd of=/dev/mmcblk0 bs=4M conv=fsync'
```

Verify the readback (takes several minutes):

```sh
ssh root@<board-ip> 'dd if=/dev/mmcblk0 bs=1M' \
  | head -c 3907452928 | sha256sum
# compare with the full-image entry in SHA256SUMS.txt
```

Then reboot from the serial console.
### Updating individual parts later (no full reflash)

Boot RAM recovery again, then use the corresponding target with the exact
Beta 2 file:

| WebUI target | File | Offset (LBA) | Expected SHA-256 |
|---|---|---|---|
| `mbr` | first 512 bytes of the full image | 0 | *(table write)* |
| `dtb` | `oc200-patched.dtb` | 0x380 (896) | see `SHA256SUMS.txt` |
| `kernel` | `oc200-linux-6.18.59-Image` | 0x400 (1024) | see `SHA256SUMS.txt` |
| `p1` | `alpine-3.24.3-rootfs.erofs` | 0x20000 (131072) | see `SHA256SUMS.txt` |
| `p2` | `alpine-3.24.3-userdata.f2fs` | 0x40000 (262144) | see `SHA256SUMS.txt` |

Over SSH the equivalents are:

```sh
# mbr (first 512 bytes of the full image)
head -c 512 oc200-alpine-3.24.3-full-emmc-image.img | \
  ssh root@<board-ip> 'dd of=/dev/mmcblk0 bs=512 conv=fsync'
# dtb
cat oc200-patched.dtb | \
  ssh root@<board-ip> 'dd of=/dev/mmcblk0 bs=512 seek=896 conv=fsync'
# kernel
cat oc200-linux-6.18.59-Image | \
  ssh root@<board-ip> 'dd of=/dev/mmcblk0 bs=512 seek=1024 conv=fsync'
# p1
cat alpine-3.24.3-rootfs.erofs | \
  ssh root@<board-ip> 'dd of=/dev/mmcblk0 bs=512 seek=131072 conv=fsync'
# p2 (slow; ~3.5 GiB over the LAN)
cat alpine-3.24.3-userdata.f2fs | \
  ssh root@<board-ip> 'dd of=/dev/mmcblk0 bs=512 seek=262144 conv=fsync'
```

After any part update, verify by reading the same range back and comparing
SHA-256, e.g. for the kernel:

```sh
ssh root@<board-ip> 'dd if=/dev/mmcblk0 bs=512 skip=1024 count=82381' | sha256sum
# compare with the kernel entry in SHA256SUMS.txt
```

## Patched bootloader artifact

`oc200-spi-nor-bootloader-patched.bin` is a full 1 MiB SPI-NOR image. It is
not an eMMC payload and is not needed for RAM recovery or any procedure above.
Flashing it changes boot firmware and carries a much higher brick risk. This
guide intentionally provides no write command for it; only use it with a
separately reviewed, board-specific procedure and a verified full-NOR backup.

## Safety rules

- Only in RAM recovery (`rdinit=/init`). The WebUI additionally refuses if
  any eMMC partition is mounted.
- Only the eMMC user area. Never `mmc write` to `boot0`/`boot1`/RPMB and
  never `sf` (NOR) writes (`saveenv` does not exist in this build; the same
  rule covers any `env save` equivalent).
- Do not flash the SPI-NOR image as part of eMMC recovery.
- Verify every write by readback SHA-256 before rebooting.

After flashing, continue with `GUIDE-NORMAL-BOOT.md`.
