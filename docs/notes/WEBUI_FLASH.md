# OC200 WebUI eMMC flash recovery (Beta-2 maintenance)

RAM recovery with a built-in web UI for uploading artifacts and flashing the
eMMC user area from the board. Intended for maintenance after Beta-2: update
kernel/DTB/p1/p2/MBR without a TFTP host or `mmc write` from U-Boot.

Artifacts (release assets, see `release/beta2/README.md`):

| Artifact | Bytes | SHA-256 |
|---|---|---|
| `oc200-squashfs-initramfs-webui.uImage` | 11,632,565 | a0b38edd7cc552ca947e4f7895e202d57024f398f9952b64406982b2167060a8 |
| `oc200-alpine-3.24.3-ssh-webui.squashfs` | 10,788,864 | 611db8cf115158b2d04f04aeb347543f6c218dee545103d22736c9a0069bef8b |

Build source trees: `ram-tree-extracted/` (squashfs content, includes
`www/` + `etc/oc200-init`) and `squashfs-initramfs/` (wrapper). The retained
non-WebUI pair (`oc200-squashfs-initramfs.uImage` /
`oc200-alpine-3.24.3-ssh.squashfs`) is untouched; it still boots to SSH only
with no httpd and no flash CGI.

## What the WebUI can and cannot write

Targets: `mbr` (LBA 0), `dtb` (LBA 0x380), `kernel` (LBA 0x400,
region up to p1), `p1` (LBA 0x20000, 64 MiB EROFS), `p2` (LBA 0x40000,
F2FS), `raw` (any 512-aligned LBA in the user area). Every final write is
aligned; `dtb`/`raw` tails are zero-padded to 512.
It **never** touches `mmcblk0boot0/boot1`, `mmcblk0rpmb`, or SPI NOR, and it
refuses to run unless booted from RAM recovery (`rdinit=` on the cmdline).

## Booting

Serve `oc200-squashfs-initramfs-webui.uImage` via TFTP and boot from the
U-Boot prompt (same procedure as `OC200_SQUASHFS_BOOT.md`, new filename),
without saving the environment:

```text
setenv ipaddr 192.168.12.240
setenv serverip 10.255.255.1
setenv gatewayip 192.168.12.1
setenv netmask 255.255.255.0
ping 10.255.255.1
tftpboot 0x05000000 oc200_test/oc200-6.18.59-persist-f2fs-ar8236-f2fs-security-Image
setenv initrd_size 0
tftpboot 0x20000000 oc200_test/oc200-squashfs-initramfs-webui.uImage
setenv initrd_size ${filesize}
tftpboot 0x04f00000 oc200_test/oc200-6.18.59-swp-ports.dtb
setenv bootargs console=ttyMV0,115200 earlycon=ar3700_uart,0xd0012000 rdinit=/init loglevel=7
booti 0x05000000 0x20000000:${initrd_size} 0x04f00000
```

Notes:

- `rdinit=/init` is mandatory — the flash CGIs gate on it.
- Stage the DTB from `not-retained/research/emmc-image/erofs-stage/`.
- Do not run `saveenv`, `mmc write`, or `sf write`.

## Usage

1. Serial console prints, after DHCP:
   `OC200 WebUI: open http://<board-ip>/ , token: <16 hex chars>`.
   DHCP runs on `lan0`, with fallback to `swp1`/`swp2` if no lease.
2. From a machine on the same LAN, open `http://<board-ip>/`, enter the token.
3. Pick target + artifact file. Expected SHA-256 for Beta-2 artifacts
   (from `beta2-build-manifest.json`/manifest used at build time):

   - dtb `fc723d1f04d0b76c1803f32fbf200f7e9a787323a61fb4ad02de1026a235ddca`
   - kernel `0d488e66bcb0fc50ca02e1e51de2c85dc7c4883a92e5fff71e4cc992a21b8af2`
   - p1 `35cf1c79d992748eded533f78aa35c74dcb98dd5acb19f0f141fe99f1fc14ed2`
   - p2 `011937d2905f30bfdd130cb9df11df53c869c263dd519ac2dfcf5e0e632beeb2`
   - raw (full image)
     `ef0540f88a01bea6f4cceedc62f00b630b0d26915602708b13fd972e43a83c69`

   For a fresh artifact (new build), leave the SHA empty — the server still
   shows the readback hash to copy-and-compare.
4. Flash → chunked upload (8 MiB chunks, server hashes each chunk, writes via
   `dd`/`seek` directly to `/dev/mmcblk0`) → automatic readback verify of
   the final SHA-256 against the expected value.
5. Reboot only when all writes verify and partitions are unmounted; the CGIs
   already refuse while any eMMC partition is mounted.

Optional hard-lock: put the board's full CID in `/etc/webui-cid` in the
squashfs (extras in the tree, `T_WEBUI_CID` in scripts) and the flash
endpoints accept writes only from that CID.

## Safety model

- Token (`/run/webui-token`) is 16 random hex chars, printed only on serial.
- `httpd` starts only in RAM recovery; `/www` is read-only inside squashfs.
- Write path: plain `dd of=… seek=…` per chunk into the user area; no
  partition table is reloaded by the kernel in RAM mode, so no re-partition
  syscalls race writes.
- Never selects anywhere but the sector ranges listed above; out-of-region or
  oversized writes are rejected by the hardcoded layout constants in
  `www/cgi-bin/common.sh` (decimal LBA/sector checks).
