# OC200 Alpine eMMC — Beta 1 build and test runbook

**Status:** workspace hand-off for the Apple Silicon + OrbStack build. The new
Beta 1 image has **not** been assembled or flashed yet. Native ARM64 APK repair
has not been run on Mac/OrbStack, and the fixed security kernel has not yet
passed the persistent `/etc` copy-up acceptance test. The recorded eMMC image
is retained as rollback/reference; RAM recovery artifacts are also retained.

**Later hardware update (2026-11-01):** a separate SPI-NOR default-environment
patch was applied to the OC200; the full-chip readback matched the patched
candidate and the user reported a successful reboot. This does not mean the
Beta 1 eMMC image was flashed or passed its runtime acceptance tests. See
`docs/notes/OC200_BOOTLOADER_INTERVENTION_2026-11-01.md` for the intervention
record and write incident details.

## Target and layout

- TP-Link OC200 / Marvell Armada 3720, 1 GiB RAM.
- Alpine Linux 3.24.3 aarch64, Linux 6.18.59, UTC, root password `alpine`.
- Persistent root: EROFS p1 lower + F2FS p2 OverlayFS upper/work.
- Interfaces `swp1`/`swp2`: DHCPv4 + IPv6 SLAAC; SSH and chrony.
- 512 MiB zram swap in RAM only; no eMMC swap. No `UsePAM`, no persistent
  `iperf3`. CPU temperature is unsupported by the available sensor binding.
- Debug packages in the EROFS base: `vim`, `curl`, `tftp-hpa`, `bind-tools`,
  `tcpdump`, `ethtool`, `strace`, `usbutils`, `f2fs-tools`.

All LBAs use 512-byte sectors in the eMMC **user area**. DOS/MBR is retained.

| Region | Start LBA | Size |
|---|---:|---:|
| DTB | `0x380` | 25 sectors (`0x19`) |
| Raw kernel | `0x400` | read window ends at p1 (`0x20000`) |
| p1: EROFS label `rootfs` | `0x20000` (64 MiB) | `0x20000` sectors = 64 MiB |
| p2: F2FS label `userdata` | `0x40000` (128 MiB) | through image end |

The new p2 start is 4 MiB aligned and gives p1 44 MiB more room than the
currently flashed layout (`0x2A000`); the larger Beta rootfs needs it (the
lz4-compression of the new full tree exceeds a 32 MiB p1). The Beta image is
7,631,744 sectors
(3,907,452,928 bytes). It is 1,638,400 bytes smaller than the eMMC user area
reported by Linux; those final bytes remain outside the image/table. The labels
are filesystem labels, not MBR partition names.

An earlier prototype proposal used ext4 for p2. After the later board tests,
Beta 1 uses F2FS; its kernel must have both `CONFIG_F2FS_FS_XATTR=y` and
`CONFIG_F2FS_FS_SECURITY=y` so OverlayFS can copy up lower-layer security
xattrs. The retained flash image uses the older kernel without the security
option; the separate fixed kernel below is required for Beta 1.

Moving p2 creates a new filesystem and **erases all existing p2 overlay data**
when flashed. Build and test before any flash operation.

## Fixed inputs and retained artifacts

Run commands from the copied `oc200-linux` workdir. This is not a Git checkout;
keep an untouched copy before experimenting.

| Beta input | Path | Bytes | SHA-256 |
|---|---|---:|---|
| Persistent kernel with F2FS security-xattr fix | `[đã xóa] <archive>/research/emmc-image/erofs-stage/oc200-6.18.59-persist-f2fs-ar8236-f2fs-security-Image` | 42,179,072 | `d1d0cc1aca3c4b48958493c2c6e10a09f08920038b1da7ec8e8adc2c579ba3c6` |
| Current OC200 DTB (`swp1`/`swp2`, USB, Cloud LED) | `[đã xóa] <archive>/research/emmc-image/erofs-stage/oc200-6.18.59-swp-ports.dtb` | 12,381 | `fc723d1f04d0b76c1803f32fbf200f7e9a787323a61fb4ad02de1026a235ddca` |
| Prepared Alpine rootfs | `[đã xóa] <archive>/research/alpine/3.24.3/ssh-rootfs/` | — | mutable build input |
| Kernel source/config | `<archive>/research/kernel/linux-6.18.59/`, `build/kernel/oc200-alpine-full.config` | — | source includes OC200 patches |
| Resolved build config snapshot | `build/kernel/oc200-6.18.59-resolved.config` | — | exact config used for the staged security kernel |

Do not rebuild or substitute the fixed kernel/DTB for Beta 1. The retained full
image `[đã xóa] <archive>/research/emmc-image/oc200-erofs-persist.img` is 3,907,452,928 bytes,
SHA-256 `a958986e2e8d8cb50fe0a20d5e43c14a3bada2d987f2edb72943e08e23b8f5db`.
Its manifest `build/emmc-image/build-manifest.json` identifies the earlier
kernel (`13eb0d3f76aa8d80634db20a69b1d159942265ce0a98ea6101e44e12d6815554`)
and old p2 start `0x2A000`; it is a rollback/reference image, **not Beta 1**.
`build/emmc-image/last-flash-readback.txt` records a matching image-prefix
hash.

Keep the RAM recovery pair and image:

| Artifact | Path | Bytes | SHA-256 |
|---|---|---:|---|
| Recovery kernel | `[đã xóa] <archive>/research/kernel/oc200-6.18.59-alpine-full-usb-Image` | 40,327,680 | `97ce21c126ec6e4a312674238ff27f7b8bc0154d5e4886f69266f24740eda62e` |
| SquashFS initramfs (embeds the rootfs) | `[đã xóa] <archive>/research/alpine/3.24.3/oc200-squashfs-initramfs.uImage` | 11,732,990 | `4e78cc46a733a6d721749c022646c114694666b9e88634074b1f5a71b6d04424` |
| Standalone SquashFS | `[đã xóa] <archive>/research/alpine/3.24.3/oc200-alpine-3.24.3-ssh.squashfs` | 10,915,840 | `92057995d63c8265a0085a1c5c11045a9fa25dedf414c69de115786a599f7960` |

The DTB in the fixed-input table is also the current recovery DTB. Preserve
original captures under `<archive>/research/backups/oc200-emmc-2026-11-01/`; they are
safety backups, not build outputs. Those reads were EOF-sized, not padded
complete device images. The user-area dump is 3,907,452,928 bytes
(SHA-256 `5f98143c8f95874499a22a2b474a6229b8b5ec33de0d93d1bdc54ef13a3d39ad`);
boot0 is 2,359,296 bytes (`2dce229d0099e7e3cc65ee98b92893c8618bb92ccec498c09ade0da8ff28a9df`)
and boot1 is 2,293,760 bytes
(`f9f69f2afaff3dbcd98b095f62d13b5cfcd9eb032d5c4dee39cab25912abc9ac`). The
SPI-NOR reads are also retained. These files preserve the actual bytes returned
by `dd`, not synthesized or padded logical-device images; RPMB was not
accessed. Check `metadata.json` and the backup README before using a dump.

The kernel source/config and the minimal persistent initramfs sources remain
in the workspace. The fixed kernel was built with the small embedded BusyBox
`/init` at `[đã xóa] <archive>/research/alpine/3.24.3/erofs-initramfs/init`; it mounts EROFS plus
F2FS OverlayFS, then switches to the prepared Alpine root and standard OpenRC.
This Beta locks the existing kernel artifact; the fixed kernel has not yet
passed the persistent copy-up test on the board. Do not replace its initramfs
or rebuild it as part of this handoff.

## Apple Silicon + OrbStack

OrbStack exposes a Docker-compatible engine and supports bind mounts and
multi-architecture images. Force native ARM64 for APK triggers: an earlier
x86_64 container attempt failed with `Exec format error` in the BusyBox trigger
and left `ssh-rootfs` partially installed.

From the workdir on macOS:

```sh
docker context show
docker run --rm --platform linux/arm64 alpine:3.24.3 \
  sh -c 'uname -m; cat /etc/alpine-release'
```

Expect `aarch64` and `3.24.3`. Stop if the architecture is `x86_64`/`amd64` or
the Alpine release is unexpected. Do not use Rosetta or `apk --no-scripts`.

Start the native build container:

```sh
docker run --rm -it --platform linux/arm64 \
  --mount "type=bind,src=$PWD,dst=/work" \
  --mount "type=bind,src=[đã xóa] <archive>/research/alpine/3.24.3/ssh-rootfs,dst=/rootfs" \
  -w /work alpine:3.24.3 sh
```

The Beta build uses the fixed kernel/DTB artifacts and does not rebuild the
kernel. Inside the container install the rootfs repair and filesystem tools:

```sh
apk add --no-cache python3 coreutils util-linux f2fs-tools erofs-utils
```

Keep this container native ARM64 through APK repair and package installation.
The rootfs is bind-mounted from macOS: first verify the container can create
files and preserve the required ownership/mode bits there. If APK repair/audit
or the `4755` mode check fails due to bind-mount permissions or xattr
semantics, stop and move the rootfs to a Linux-native filesystem/volume for
repair and EROFS creation. Do not bypass APK scripts or triggers.

## Repair and populate the rootfs

`ssh-rootfs` is already the prepared persistent base; do not re-extract the
minirootfs over it. For Beta 1 it was kept untouched as the config-migration
reference and the tree was instead rebuilt fresh as
`[đã xóa] <archive>/research/alpine/3.24.3/beta1-rootfs/` (native ARM64 container, all packages
reinstalled, configs migrated from `ssh-rootfs`, chrony UID pinned at 124).
The p1 build above uses `beta1-rootfs`. If rerunning any of this, still use
the target root's repositories and signing keys:

```sh
apk --root /rootfs --arch aarch64 \
  fix

apk --root /rootfs --arch aarch64 add \
  vim curl tftp-hpa bind-tools tcpdump ethtool strace usbutils f2fs-tools
```

With `--root /rootfs`, APK reads that root's `/etc/apk/repositories` and
trusted-key directories by default. Do not point it at the container's host
repositories or keys.

`atftp` is not in Alpine v3.24; `tftp-hpa` provides the `tftp` client. Package
versions observed in the 2026-11-01 v3.24 aarch64 indexes: vim `9.2.1202-r0`,
curl `8.23.0-r0`, tftp-hpa `5.2-r7`, bind-tools `9.20.30-r0`, tcpdump
`4.99.6-r1`, ethtool `7.0-r0`, strace `6.19-r1`, usbutils `019-r0`, and
f2fs-tools `1.16.0-r4`. Repositories can advance; record actual versions in
the release manifest.

APK fix/add and triggers must exit 0 without `Exec format error`. Verify:

```sh
apk --root /rootfs --arch aarch64 audit
apk --root /rootfs --arch aarch64 info -v \
  vim curl tftp-hpa bind-tools tcpdump ethtool strace usbutils f2fs-tools
find /rootfs/bin /rootfs/sbin /rootfs/usr/bin /rootfs/usr/sbin \
  -type f \( -name vim -o -name curl -o -name tftp -o -name dig \
  -o -name tcpdump -o -name ethtool -o -name strace -o -name lsusb \
  -o -name mkfs.f2fs -o -name fsck.f2fs \) -print
stat -c '%a %n' /rootfs/bin/more /rootfs/bin/tload
```

`more` and `tload` must remain mode `4755`. Audit may report intentional local
configuration changes; review these, but there must be no missing package
payloads, BusyBox applets, trigger errors, or unexplained mode mismatches. Do
not install persistent `iperf3`.

The persistent setup script
`[đã xóa] <archive>/research/alpine/3.24.3/erofs-initramfs/prepare-rootfs-persistent.sh` documents
base configuration. The current rootfs is already prepared; rerun that script
only when deliberately bootstrapping a fresh rootfs, then recheck root
password, interfaces, OpenRC links, SSH, chrony, zram, UTC, and package modes.

## Build p1 and p2

Inside the container, from `/work`:

```sh
cd /work
test ! -e [đã xóa] <archive>/research/emmc-image/erofs-stage/p1-beta1.erofs
test ! -e [đã xóa] <archive>/research/emmc-image/erofs-stage/p2-beta1.f2fs
mkfs.erofs -z lz4hc,12 -L rootfs --all-root --preserve-mtime \
  [đã xóa] <archive>/research/emmc-image/erofs-stage/p1-beta1.erofs \
  [đã xóa] <archive>/research/alpine/3.24.3/beta1-rootfs

P2_START=262144       # 0x40000
IMAGE_SECTORS=7631744
P2_SECTORS=$((IMAGE_SECTORS - P2_START))
P2_BYTES=$((P2_SECTORS * 512))
truncate -s "$P2_BYTES" [đã xóa] <archive>/research/emmc-image/erofs-stage/p2-beta1.f2fs
mkfs.f2fs -f -l userdata [đã xóa] <archive>/research/emmc-image/erofs-stage/p2-beta1.f2fs
fsck.f2fs -f [đã xóa] <archive>/research/emmc-image/erofs-stage/p2-beta1.f2fs

fsck.erofs [đã xóa] <archive>/research/emmc-image/erofs-stage/p1-beta1.erofs
blkid -p [đã xóa] <archive>/research/emmc-image/erofs-stage/p1-beta1.erofs
blkid -p [đã xóa] <archive>/research/emmc-image/erofs-stage/p2-beta1.f2fs
stat -c '%s %n' [đã xóa] <archive>/research/emmc-image/erofs-stage/p1-beta1.erofs \
  [đã xóa] <archive>/research/emmc-image/erofs-stage/p2-beta1.f2fs
```

Labels must read `rootfs` and `userdata`; p1 must be at most 67,108,864 bytes
(64 MiB); p2 must be exactly 3,773,235,200 bytes. Stop if either filesystem
check or label verification fails.

## Assemble Beta 1

`build/emmc-image/build-image.py` is the offline Beta assembler. It writes a
new `[đã xóa] <archive>/research/emmc-image/oc200-alpine-3.24.3-beta1.img` and
`[đã xóa khỏi repo] <archive>/research/emmc-image/beta1-build-manifest.json`; it must not replace the
rollback image. It checks the fixed kernel/DTB hashes, payload bounds, MBR
layout, image size, and records component hashes.

```sh
cd /work
python3 build/emmc-image/build-image.py
python3 -m json.tool [đã xóa khỏi repo] <archive>/research/emmc-image/beta1-build-manifest.json
sha256sum [đã xóa] <archive>/research/emmc-image/oc200-alpine-3.24.3-beta1.img
```

Verify before any board operation:

- image size is 3,907,452,928 bytes;
- kernel and DTB hashes match the fixed inputs above;
- p1 starts at `0x20000`, p2 at `0x40000`, p1 spans `0x20000` sectors;
- MBR signature is `55 aa`, both types are `0x83`, and p2 ends at sector
  7,631,744;
- p1/p2 checks and labels pass;
- image hash is in the manifest and copied to a second location before release.

Do not rebuild or substitute the kernel/DTB for Beta 1. Stop if either fixed
hash differs.

## Boot and runtime validation

### RAM/TFTP recovery

Use the retained SquashFS recovery files. The old U-Boot TFTP client used here
needs the compatibility-server behavior in `<archive>/oc200-rfc2217/tftp_compat_server.py`;
the historical Linux TFTP host was `10.255.255.1`. OrbStack does not recreate
the Linux host's UDP/69 forwarding or RFC2217 route. Configure and verify a
TFTP endpoint reachable from the board on the Mac network first; use its actual
address, not the historical IP blindly.

At `Marvell>>`, stop autoboot with Ctrl-B. Previous lab values were board
`192.168.12.240`, gateway `192.168.12.1`, TFTP host `10.255.255.1`:

```text
setenv ipaddr 192.168.12.240
setenv serverip 10.255.255.1
setenv gatewayip 192.168.12.1
setenv netmask 255.255.255.0
ping ${serverip}
tftpboot 0x05000000 oc200_test/oc200-6.18.59-alpine-full-usb-Image
setenv initrd_size 0
tftpboot 0x20000000 oc200_test/oc200-squashfs-initramfs.uImage
setenv initrd_size ${filesize}
tftpboot 0x04f00000 oc200_test/oc200-6.18.59-swp-ports.dtb
setenv bootargs console=ttyMV0,115200 earlycon=ar3700_uart,0xd0012000 rdinit=/init loglevel=7
booti 0x05000000 0x20000000:${initrd_size} 0x04f00000
```

Use serial console throughout. RAM recovery is non-persistent. Do not use the
vendor HTTP update/debug path. Stage the exact fixed DTB from this workspace
in the TFTP export directory under the shown name and verify its hash first.

### Serial and bootloader precautions

U-Boot console is 115200 8N1. The board UART logic was measured at 2.5 V;
U13/MAX3373E was bypassed for the lab connection, so use a suitable level
shifter and never connect a 5 V UART signal directly to the board. The lab
bridge joined U13 pins 5↔8 and 4↔1 and used a TXS0108E with VCCA=2.5 V, VCCB
from the USB-UART adapter, OE=VCCA and common ground. Header pin order was not
independently measured; verify TX/RX/GND before wiring. The U-Boot
environment CRC was bad and it falls back to generic defaults. Manual `setenv`
values live only in RAM; `saveenv` writes SPI-NOR and is prohibited here.

Manual `tftpboot` + `booti` of unsigned kernel/DTB files has booted successfully
and does not use the vendor firmware update/debug path. The observed
`TP-LINK_SMB_GATEWAY_RSA2048_PSS` challenge belongs to the userspace `debug`
authorization command; it is not evidence that manual U-Boot boot enforces
image signatures. This does not establish how vendor HTTP recovery validates
images. No bootloader patch or NOR write is part of Beta 1.

### Manual persistent boot

After the Beta image is installed, boot manually from U-Boot:

```text
setenv loadkernel 'mmc dev 0; mmc read ${kernel_addr} 0x400 0x1fc00'
setenv loadfdt 'mmc dev 0; mmc read ${fdt_addr} 0x380 0x19'
setenv bootargs 'console=ttyMV0,115200 earlycon=ar3700_uart,0xd0012000 root=/dev/mmcblk0p1 rootwait rw'
run loadfdt
run loadkernel
booti ${kernel_addr} - ${fdt_addr}
```

`0x1fc00` ends the kernel read at p1 LBA `0x20000`; the vendor's old `0x8000`
window is too short. `setenv` changes RAM environment only. Never run
`saveenv`.

### Acceptance checklist

Save serial output and run:

```sh
uname -a
cat /proc/cmdline
grep -E 'overlay /|mmcblk0p[12]' /proc/mounts
blkid /dev/mmcblk0p1 /dev/mmcblk0p2
touch /etc/.beta-overlay-probe && rm /etc/.beta-overlay-probe
apk audit
apk add tree
rc-status -a
ip -br addr
cat /proc/swaps
cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq
date -u
ls -l /etc/ssh/ssh_host_*_key
lsusb
dmesg | grep -Ei 'xhci|usb|f2fs|erofs|overlay|error'
```

Expect: overlay `rw`; labels `rootfs`/`userdata`; p2 F2FS; `swp1`/`swp2`
receive DHCPv4/SLAAC; about 512 MiB zram is the only swap; CPUFreq sysfs
exists; networking, sshd, and chronyd run; `/etc` writes and APK transaction
succeed; USB enumerates; Cloud LED uses `disk-activity`, Power LED unchanged.
Reboot and verify package/file persistence. Clock-skew warnings may precede
chrony sync; verify UTC afterwards.

The AR8236 flow-control change is included but its throughput effect is not
proven. If measuring, use an external iperf3 endpoint; do not install iperf3
persistently. The switch is 10/100 Mbps; reverse traffic around 94–95 Mbps is
near line rate. Record forward/reverse results and interface counters.

## Flash boundaries — only after Beta acceptance

Flashing is not part of the build command. A full-image write replaces the MBR,
p1, and p2 and erases p2's current upper/work data. Use only verified RAM
recovery and the eMMC **user-area** device. Before any write confirm:

1. recovery root is RAM OverlayFS (`upperdir=/ram/upper`);
2. CID and user-area size match the intended test board;
3. no eMMC user partitions are mounted;
4. Beta image size/hash match the manifest;
5. serial console and working recovery TFTP are available.

Do not write SPI-NOR, eMMC `boot0`/`boot1`, or RPMB. Never run `saveenv`. Do
not reuse this workspace's hard-coded CID for another tester. The existing
`build/emmc-image/flash/flash-persist.sh` has no default image; it requires the
image path as its first argument. It is gated for the recorded board and is not
wired for Beta 1. Review/adapt all CID/size gates for each test board before use. Read back the full logical user area and verify
the image-length prefix hash before rebooting.

## Known state and release record

- Last full image hash: `a958986e...`; its prefix readback matched. It has old
  p2 start `0x2A000` and the pre-fix kernel.
- The fixed kernel/DTB pair above is the Beta baseline; the Beta image still
  needs new rootfs, labels, and partition boundary.
- Earlier writes targeted `/dev/mmcblk0` user area only. SPI-NOR, eMMC
  boot0/boot1, and RPMB were not written. Original captures remain under
  `<archive>/research/backups/oc200-emmc-2026-11-01/`.
- The separate SPI-NOR default-environment intervention is documented in
  `docs/notes/OC200_BOOTLOADER_INTERVENTION_2026-11-01.md`; the NOR now matches
  the patched candidate, not the original dump hash. This does not alter the
  statement above about earlier eMMC writes or make the Beta 1 image accepted.
- UART is 115200 8N1, measured about 2.5 V; use a suitable level shifter. The
  stored U-Boot environment CRC was bad; set network/boot variables manually.
- Original user/boot-area captures are EOF-sized reads, not complete padded
  device images. The userspace RSA-PSS challenge is not proof of manual U-Boot
  signature enforcement.
- Previous tests used NAT. DHCP or board-to-host ping does not guarantee
  host-to-board SSH reachability.
- Preserve AR8236 patch provenance. Previous pause/offload A/B tests did not
  materially improve forward throughput.
- Tester reports should include board CID masked, image hash, serial log,
  mounts, APK audit, service status, network, zram, CPUFreq, USB, and LED.
- Original source provenance: TP-Link GPL archive
  `<archive>/research/source/gpl_oc200.tar.bz2` (SHA-256
  `f73f38857a661f481f7a56df67c5b6d6695fbb4dc580b9f7d2546806fd9524bc`),
  Alpine v3.24.3 aarch64 netboot archive (SHA-256
  `0ac085f19648562d33deb8bfdc339d747e40f456a49a43385d468ea46a888147`),
  and minirootfs archive (SHA-256
  `4d3f80afaebd447c5591a63a73bde380a149fe8b2d85488cafabb2ff97551f50`).

Expected Beta build outputs:

```text
[đã xóa] <archive>/research/emmc-image/erofs-stage/p1-beta1.erofs
[đã xóa] <archive>/research/emmc-image/erofs-stage/p2-beta1.f2fs
[đã xóa] <archive>/research/emmc-image/oc200-alpine-3.24.3-beta1.img
[đã xóa khỏi repo] <archive>/research/emmc-image/beta1-build-manifest.json
```

Keep the current image and recovery pair untouched until the Beta image passes
all checks and its manifest/hash is copied elsewhere.

`<archive>/oc200-rfc2217/OC200_credentials.txt` contains a plaintext lab credential. Do
not publish it or include it in a shared archive; protect personal workspace
copies accordingly.
