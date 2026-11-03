# OC200 eMMC Alpine installation plan

> Historical engineering log. Its intermediate layouts, image hashes and
> pending-status statements are snapshots from earlier stages and are not the
> current Beta 1 procedure. Use workspace-root `BETA_BUILD.md` for the active
> target, image builder, recovery path and flash boundaries.

## Goal

Replace the vendor eMMC user-area layout with a near-default Alpine aarch64
installation while preserving SPI-NOR and the existing U-Boot/TF-A boot chain.
Use the currently running Alpine RAM/TFTP system as the flasher only after all
preflight gates pass.

## Evidence from the read-only dump

Device:

```text
Samsung 4FTE4R
CID 15010034465445345201c013bc16b600
CSD d02701320f5903fff6dbffef8e404000
Linux user area reported: 3,909,091,328 bytes
U-Boot user area: approximately 3.6 GiB
```

U-Boot raw boot offsets:

```text
DTB:    LBA 0x380, 128 sectors
Kernel: LBA 0x400, 32768 sectors
Rootfs: LBA 0x10400
```

Observed MBR:

```text
p1 start 66560,   size 1047552 sectors  (~511.5 MiB)
p2 start 1114112, size 1024 sectors    (512 KiB)
p3 start 1115136, size 2048 sectors   (1 MiB)
p4 start 1117184, size 7200768 sectors
```

Content inspection:

- p1 starts with SquashFS magic `hsqs`; it is the vendor rootfs.
- p2 and p3 are zero/metadata-sized areas in the dump.
- p4 has an ext4 superblock at partition offset 1024 and contains vendor data.
- The old p4 MBR entry extends beyond the actual EOF returned by `dd`; this is
  already reported by the kernel as a truncated partition.
- `mmcblk0boot0` and `mmcblk0boot1` are separate eMMC boot areas. They are not
  part of the user-area MBR and must not be written in the first installation.
- RPMB was not read and must not be touched.

Dump directory:

```text
user.img.zst
boot0.img.zst
boot1.img.zst
oc200-spi-nor-1MiB.bin.zst
oc200-emmc-dtb-region-lba-0x380-128sectors.bin.zst
```

The raw files are EOF-sized reads. `checksum-comparison.json` proves that
zero-padding each raw file to the corresponding Linux logical size produces the
same SHA-256 as a direct board-side hash.

## Recommended target layout

Do not preserve the vendor p1–p4 layout. Use a clean MBR layout within the
actual user area, with a small raw boot gap retained for U-Boot compatibility:

```text
LBA 0                 MBR
LBA 0x380             optional DTB raw copy for U-Boot compatibility
LBA 0x400             optional kernel raw copy for U-Boot compatibility

p1                   ext4 Alpine root filesystem
p2                   ext4 or swap/data, optional
p3                   data, optional
p4                   remaining data, optional
```

The first implementation should prefer a single ext4 root partition beginning
at or after the existing p1 start sector, because the current U-Boot reads a
raw kernel and DTB but does not load an initramfs. A direct ext4 root avoids the
current SquashFS/OverlayFS wrapper complexity.

The exact final starts/sizes must be generated from the real logical user-area
size and checked against the read-only dump. Do not reuse the old p4 end
without correcting its out-of-device boundary.

## Kernel/rootfs boot design

The cleanest persistent boot path is:

1. Write the kernel raw copy at LBA `0x400`, within the existing U-Boot read
   window (`0x8000` sectors).
2. Write the DTB raw copy at LBA `0x380`, within the existing U-Boot read
   window (`0x80` sectors).
3. Install Alpine rootfs as ext4 on p1.
4. Boot with:

```text
setenv bootargs console=ttyMV0,115200 earlycon=ar3700_uart,0xd0012000 root=/dev/mmcblk0p1 rootwait rw
booti $kernel_addr - $fdt_addr
```

This is a test of the existing U-Boot raw boot path and does not require
signature bypassing. The environment must not be saved until the board has
booted successfully from the new eMMC image multiple times.

The kernel must include built-in support for:

```text
CONFIG_BLOCK=y
CONFIG_EXT4_FS=y
CONFIG_FILE_LOCKING=y
CONFIG_MMC=y
CONFIG_MMC_BLOCK=y
CONFIG_MMC_SDHCI=y
CONFIG_MMC_SDHCI_PLTFM=y
CONFIG_MMC_SDHCI_XENON=y
CONFIG_MSDOS_PARTITION=y
CONFIG_SERIAL_MVEBU_CONSOLE=y
CONFIG_NET_DSA_AR8236=y
CONFIG_NET_DSA_TAG_QCA=y
CONFIG_MVNETA=y
```

The current Alpine-baseline OC200 config also includes USB host/storage
support, OverlayFS/TMPFS, namespaces, cgroups, keys and crypto for normal
userspace operation.

## Using Alpine RAM as flasher

The running RAM system can be used to write the user area, but only through a
carefully bounded, externally verified stream. Prefer sending a prepared image
from the host to the board over SSH rather than constructing a multi-gigabyte
image in board RAM.

Required safeguards:

1. Keep the running rootfs entirely in RAM/TFTP.
2. Keep the SSH session alive from the host and use a second serial console
   observer.
3. Verify the target device is exactly `/dev/mmcblk0`, never a partition,
   boot0, boot1 or RPMB.
4. Stop all services that might access eMMC partitions.
5. Unmount any `/dev/mmcblk0p*` mounts before writing.
6. Make the host hold the complete image and SHA-256 manifest.
7. Stream with a bounded buffer and check the remote write exit status.
8. Re-read/verify selected regions after every write phase.
9. Do not write the MBR until kernel/DTB/rootfs payloads are independently
   verified and the recovery path has been tested.
10. Never run `saveenv`, `sf write`, `sf erase`, `mmc bootpart`, or any RPMB
    command as part of the first installation.

Recommended write sequence:

```text
A. Write/test rootfs payload to an unused or staged target only if a recovery
   path remains available.
B. Verify the ext4 filesystem can be mounted/read-only from RAM Alpine.
C. Write raw kernel and DTB at the U-Boot offsets.
D. Re-read and hash kernel/DTB from eMMC.
E. Write and verify the partition table.
F. Reboot to U-Boot, stop autoboot, and boot the raw kernel/DTB manually.
G. Only after successful repeated boots consider making boot environment
   changes; saving environment writes SPI-NOR and remains a separate step.
```

For a truly destructive whole-device replacement, the safer order is to first
prepare a host-side image and validate it with `fdisk`, `blkid`, `file`,
`debugfs` and hashes. Then use one controlled sequential write to `/dev/mmcblk0`
with `oflag=direct`, followed by a full or sampled readback. This must be a
separately confirmed execution step; this document does not authorize it.

## Recovery gates

Do not proceed to destructive writes until all are true:

- A valid host-side dump/hash record exists and is stored separately.
- The exact final target image has been reviewed.
- The kernel/DTB/rootfs have booted from TFTP on this board.
- U-Boot manual recovery through TFTP is confirmed after a cold boot.
- The user understands that replacing p1/p4 destroys the vendor firmware.
- SPI-NOR will remain untouched and U-Boot will be used manually if needed.
- The board-side write command has been reviewed as a separate explicit action.

## Current decision

Prepare and validate the Alpine ext4 image first. Do not erase or write the
board in the current step. The existing SquashFS image is a successful RAM/TFTP
bring-up artifact, but it is not the preferred persistent eMMC rootfs because
it requires an initramfs OverlayFS wrapper and cannot use the stock Alpine
boot/init flow directly.

## Implementation 2026-11-01 (EROFS + ext4 overlay, plan A shifted)

Supersedes the direct-ext4 layout above; approved direction is:
read-only EROFS p1 rootfs over an ext4 persistent OverlayFS layer.
Kernel stays raw-loaded at 0x400 and p1 was moved far enough to give the
kernel long-term headroom plus round boundaries.

Final layout (LBA, 512-byte sectors, device = 7,631,744 sectors):

```text
0x000     MBR (disk id 0x0c20bcd1)
0x380     DTB, 25 sectors (0x19)
0x400     raw kernel, region until 0x20000 (reads use 0x1fc00 sectors)
0x20000   p1 start, 40960 sectors (20 MiB)  = EROFS Alpine base (ro)
0x2A000   p2 start, 7459712 sectors        = ext4 persist (upper/work)
          p2 end == 7631743, the exact device end
```

Artifacts (Beta 1 intermediates, not retained):

```text
oc200-erofs-persist.img     3907452928 bytes
sha256 bf4f45b7c9fbdcd4500e83708eb2bfcb861d1bf3193b4fdec91cf90f3267f086
build-image.py              offline assembler; build-manifest.json per part
erofs-stage/p1.erofs        17776640 bytes  f76b2abb...
erofs-stage/p2.ext4         3819372544 bytes a1a0bdd6...
erofs-stage/persist-Image   41318912 bytes  c311f506... (kernel, EROFS=y,
                            OverlayFS=y, embedded initramfs with musl loader)
flash/flash-persist.sh      prepared helper (actual flash used guarded SSH stream)
uboot-persist-boot.md       U-Boot setenv & TFTP preflight runbook
```

Rootfs changes versus the RAM bring-up tree: /etc/oc200-init removed,
OpenRC inittab + runlevels enabled (devfs/dmesg sysinit; hostname, modules,
sysctl, localmount, loopback, machine-id, seedrng, hwclock, bootmisc boot;
networking, sshd, chronyd default; mount-ro/killprocs/savecache shutdown),
sshd init creates /run/sshd, resolv.conf blanked, hostname `oc200`,
chrony+UTC tzdata kept, no iperf3, no UsePAM. Verified offline by
fsck.erofs --extract with TREE_MATCH against the source tree.

## Installation run — 2026-11-01

Before writing, the latest kernel successfully mounted the staged p1 EROFS
image and built a writable OverlayFS root over a RAM tmpfs upper/work layer.
The board had no enumerated USB root hubs/devices; dmesg showed usbcore driver
registration only (no xHCI/DWC3 controller attach). No USB devices were
plugged/unplugged or otherwise changed during this check.

SPI-NOR was present as `/dev/mtd0ro`, size 1 MiB. Two independent read-only
SSH dumps compared byte-for-byte equal and are retained at:

```text
oc200-spi-nor-1MiB.bin
SHA-256 56715aafa699c576d11dc36410dfa7854256d5d65293695117c5e6d0b75d5151
```

The authorized initial eMMC install was streamed to `/dev/mmcblk0` over SSH after
checking the CID, device size, RAM root, and absence of mounted eMMC
partitions. The full 7,631,744-sector readable user-area range was read back
and SHA-256 matched the then-current image:

```text
image / readback SHA-256:
ab38e815b3e98c71042a055a5e8cb117266ba0a5d4a812e9a23fc2b5af05414e
```

The first persistent kernel then panicked because Alpine BusyBox in the
embedded initramfs is dynamically linked and its musl loader was absent;
`/init` therefore failed with `ENOENT`. A corrected kernel was built with the
musl loader and libc symlink included. The corrected full image now has SHA-256
`538be17d244b5394be6eb7271c4e982bec5ce3a1e58cfd4141e7af62e452f5ac`, and its
kernel is staged at
`/var/lib/tftpboot/oc200_test/oc200-6.18.59-persist-fixed-Image`.

No SPI-NOR, eMMC boot0/boot1, or RPMB writes were made. The eMMC initially
received the full image whose readback hash is above; the raw kernel was later
replaced with the corrected kernel as recorded below. Persistent boot/service
verification remains pending. Do not `saveenv`.

Correction applied over SSH from Alpine RAM at `192.168.12.27`:

- Confirmed `/` was RAM OverlayFS and no `/dev/mmcblk0p*` filesystem was
  mounted; CID matched the backed-up device.
- Wrote only the corrected 41,318,912-byte kernel at raw LBA `0x400` using
  `dd` with `conv=notrunc,fsync`. Its end is byte 41,843,200, safely before
  p1 at byte 67,108,864 (LBA `0x20000`).
- Read back the exact 80,701 sectors from LBA `0x400`; SHA-256 matches:
  `c311f50699268efa116528e8fa4a81afcec16a45abc95975a73196f3e0658066`.
- Persistent eMMC boot was subsequently confirmed by the user using the
  `loadkernel` read count `0x1fc00`. The partial OpenRC log shows DHCP
  succeeded, but chronyd and sshd failed; see the service-failure notes in the
  current session. No SPI-NOR or eMMC boot-partition writes were performed.

## USB DTB experiment — 2026-11-01

The runtime DTB showed `/soc/bus@d0000000/usb@58000` (Armada 3700 xHCI) and
`usb@5e000` (EHCI) as `disabled`. The OC200 DTS had instead enabled similarly
named nodes under a different `/soc/internal-regs` branch. Added explicit
`&usb3 { status = "okay"; };` and `&usb2 { status = "okay"; };` overrides.

The new DTB compiled successfully; decompilation confirms both actual host
controller nodes have status `okay`. It is 12,331 bytes, fits in the 25-sector
U-Boot load window, and its SHA-256 is
`54281ed4d006f993f4ecd1ea2c45142129a9d56acbc439d0257eb0a0f536bc72`.

The old TFTP DTB remains untouched:

```text
/var/lib/tftpboot/oc200_test/oc200-6.18.59-final.dtb
SHA-256 7b025fe2118929aaa220b399257b469a0512b0071432abe990bdd5f94b8e9520
```

New DTB is separately staged at
`/var/lib/tftpboot/oc200_test/oc200-6.18.59-usbfix.dtb`.
Before flashing, the full 64 KiB raw DTB window at LBA `0x380` was saved as
`oc200-emmc-dtb-region-lba-0x380-128sectors.bin`;
its prefix matched the old TFTP DTB. The new DTB was then padded to 64 KiB,
written only to that raw window, and read back with matching SHA-256
`849159cfaeb504f889d4e358879f28001121b6b41f7b9eacc16718c49f6ce20a`.

The user confirmed a successful boot with the corrected raw eMMC kernel and
DTB. `lsusb -t` reports the xHCI USB 2.0 root hub (`1d6b:0002`) and USB 3.0
root hub (`1d6b:0003`) on buses 001 and 002. This confirms the xHCI controller
and both root hubs initialize. The supplied output lists only the root hubs;
downstream peripheral enumeration still needs verification with a device
connected to the OC200 USB host port.

## Service and eMMC-activity LED rebuild — 2026-11-01

Prepared and flashed a corrected full user-area image after the first
persistent boot exposed OpenRC/chrony/sshd problems. The rootfs now removes the
invalid `dev` dependencies from `hwdrivers` and `machine-id`, creates the
`chrony` system account (UID/GID 124), points sshd host keys to `/etc/ssh`
(where Alpine's OpenRC keygen writes them into the persistent overlay), and
does not start the `modules` or `hwclock` services. The image does not ship a
module tree; required OC200 USB EHCI drivers are now built in instead.

The user identified the upper Cloud LED as the requested indicator and the
lower Power LED as always on. The existing vendor-derived `system-led` node
uses MPP1 and corresponds to the controllable status LED. It now selects the
`disk-activity` trigger; `drivers/mmc/core/block.c` calls that trigger for
MMC/eMMC reads and writes. `CONFIG_LEDS_GPIO=y` and
`CONFIG_LEDS_TRIGGER_DISK=y`. The Power LED is unchanged.

Build verification: `Image` and all DTBs compile; decompilation shows the
USB xHCI/EHCI nodes are `okay` and the Cloud LED has the disk-activity trigger.
The extracted EROFS passes `fsck.erofs`; p1 is 20,324,352 bytes, within its
20 MiB partition. Image layout and hashes are in
`build/emmc-image/build-manifest.json`. After correcting root's expired
password marker while preserving its password hash, the updated full image
SHA-256 is `89948e7db964ef13ec84871e05be3c4e7f26ec7797cf7b762af7fb06e5592839`;
the p1 payload SHA-256 is
`c69fa31713f6f7a21220f18641675a7c4976cb2b3383fdffdc054abd9571ff81`.

The full-image flasher writes only `/dev/mmcblk0` (the user area), and its
preflight now requires the known RAM OverlayFS (`upperdir=/ram/upper`), expected
CID/size, and no mounted eMMC partitions. **A full image reflash resets p2 and
erases its previous persistent overlay contents. The board was manually booted
into known-good RAM/TFTP Alpine at `192.168.12.27`; preflight confirmed the
expected CID, RAM OverlayFS root, and no eMMC partition mounts. Exactly
3,907,452,928 bytes were written to `/dev/mmcblk0` and read back; that initial
full-image hash is recorded in `build/emmc-image/last-flash-readback.txt`.
Root's shadow entry was then found to require a password change, so the updated
EROFS was written to `/dev/mmcblk0p1` only from RAM recovery. The current p1
readback matches the new payload hash in
`build/emmc-image/last-p1-shadowfix-readback.txt`; the pre-update p1 backup
is `not-retained/research/emmc-image/p1-before-shadowfix.backup`. The first eMMC boot then
panicked because the kernel build had an empty `CONFIG_INITRAMFS_SOURCE` and
`CONFIG_EROFS_FS=m`, despite the runbook expecting an embedded initramfs. The
config is corrected to embed `/build/erofs-initramfs.cpio` and build EROFS in.
The kernel boots `/init`, which mounts EROFS + ext4 OverlayFS. The new kernel
is 41,318,912 bytes (80,701 sectors), ending at LBA 81,725, safely before p1
at LBA 131,072. Only that kernel extent at LBA `0x400` was written; readback
SHA-256 `045849beb994ded1911b5d356395d2f4e924eab91609b9f1a2bebf9a415cdef7`
matches, and a p1 readback still matches its expected SHA-256. The original
kernel window backup is `not-retained/research/emmc-image/kernel-window-before-embedded-initramfs.backup`.
SPI NOR, eMMC boot0/boot1, RPMB, DTB and p2 remain untouched. The board is back
in RAM recovery; persistent boot and service/USB/LED behavior await another
manual eMMC boot.

## Combined F2FS, Alpine baseline, and AR8236 update — 2026-11-01

The user authorized resetting p2 and requested F2FS plus the AR8236 iperf
investigation remedy in the same update. The combined image was written from
verified RAM recovery to the eMMC user area and readback-verified on 2026-11-01.

Changes prepared locally:

- Persistent initramfs mounts p2 as F2FS. Kernel config builds F2FS, GPIO LEDs,
  EROFS, ext4, and OverlayFS in; initramfs fixes devtmpfs permissions for
  `/dev/{null,zero,full,random,urandom,tty}` before switch_root, allowing
  unprivileged chronyd to use `/dev/urandom`.
- ZRAM is built into the kernel and an OpenRC boot service configures a 512 MiB
  LZO-RLE swap device in RAM only; it does not create any eMMC swapfile.
- Rootfs MOTD is the stock Alpine welcome. `/bin/more` and `/bin/tload` retain
  their package database's expected setuid mode (4755), addressing the two
  mode mismatches found by `apk audit` and suspected to explain APK's error
  count; confirm with `apk audit` after boot.
- AR8236 `mac_link_up` clears stale directional/half-flow bits and programs
  TXFLOW/RXFLOW from phylink's negotiated directions. It does not force a
  pause policy or modify the QCA tagger/mvneta. Runtime A/B results did not
  show material improvement from disabling pause or TSO/GSO/GRO; see
  `docs/notes/OC200_AR8236_IPERF_INVESTIGATION.md`.
- A p2 F2FS image matching the existing partition boundary/capacity passed
  `fsck.f2fs` before flashing; the full image write installed it at p2.

The final candidate is `not-retained/research/emmc-image/oc200-erofs-persist.img`, SHA-256
`a958986e2e8d8cb50fe0a20d5e43c14a3bada2d987f2edb72943e08e23b8f5db`. It
contains the 42,179,072-byte kernel, updated DSA DTB labels (`swp1`/`swp2`),
EROFS p1, and F2FS p2. From RAM recovery, preflight confirmed the expected CID,
device size, RAM OverlayFS root, and no mounted eMMC partitions. Exactly
3,907,452,928 bytes were written to `/dev/mmcblk0`; full logical user-area
readback succeeded, and the image-length prefix hash matched. The final
1,638,400 bytes beyond the image were not written. SPI-NOR, eMMC boot0/boot1,
and RPMB were not accessed. Persistent boot and runtime validation (F2FS
OverlayFS, networking, zram, CPUFreq, USB/LED, APK audit, and iperf3) remain
pending. Do not run `saveenv`.

## Persistent OverlayFS write failure — 2026-11-01

Runtime probes isolated the failure to copy-up: creating and removing a file
directly in the merged root (`/.overlay-probe-*`) succeeds, but creating a
file in `/etc` returns `EOPNOTSUPP`. The flashed EROFS p1 was extracted and
confirmed to carry `security.selinux` xattrs on `/etc` and its directories
(labels inherited from the build host). OverlayFS treats `security.*` xattrs
as mandatory during copy-up; F2FS had `CONFIG_F2FS_FS_XATTR=y` but
`CONFIG_F2FS_FS_SECURITY` disabled, so its security xattr handler was absent.
This explains why the root overlay is mounted `rw` but copy-up operations fail.

The canonical kernel config now enables `CONFIG_F2FS_FS_SECURITY=y`, and the
replacement kernel built successfully (42,179,072 bytes; SHA-256
`d1d0cc1aca3c4b48958493c2c6e10a09f08920038b1da7ec8e8adc2c579ba3c6`). It is
staged under a distinct TFTP filename for a temporary boot test. The test must
confirm `/etc` copy-up, APK writes, SSH host-key generation, and persistence
before the kernel is written to eMMC. No reflash has been performed for this
correction.
