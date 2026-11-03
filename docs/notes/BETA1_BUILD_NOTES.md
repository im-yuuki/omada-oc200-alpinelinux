# Beta 1 build notes (2026-11-01)

Runbook: `docs/BETA_BUILD.md` (layout section updated to match).

## Deviations from the original runbook plan

1. Rootfs rebased fresh instead of `apk fix` on `ssh-rootfs` (user request,担心 copy 不完整).
   - Source: retained `alpine-minirootfs-3.24.3-aarch64.tar.gz` (SHA-256 verified `4d3f80af…`).
   - Tree: `<archive>/research/alpine/3.24.3/beta1-rootfs/` — `ssh-rootfs` untouched, used as migration reference.
   - Native ARM64 OrbStack container; `apk add` the old world set (+ fixes below); `prepare-rootfs-persistent.sh` applied; config migrated file-by-file from `ssh-rootfs`.
2. World delta vs old rootfs: `procps` → `procps-ng` (package renamed); added `ifupdown-ng` explicitly (needed by `networking` init + `use ipv6-ra`; old tree had it as a dep).
3. chrony UID pinned to old layout 124:124 (chrony package auto-created 100:101; explicit sed + `chown -R 124:124 var/lib/chrony`).
4. Password: `chroot chpasswd` root:alpine (old rootfs had NO /etc/shadow — flag for testers); root shell `/bin/bash`; `ttyMV0` appended to securetty; `/etc/localtime -> /usr/share/zoneinfo/Etc/UTC`.
5. **Layout change (user-directed):** p1 span `0x20000` sectors = 64 MiB, p2 starts `0x40000` (128 MiB), p2 = 7,369,600 sectors = 3,773,235,200 bytes. Reason: full rootfs (102.7 MB) with fast-lz4 EROFS = 56 MB > 32 MiB old p1; even lz4hc,12 = 52,043,776 bytes needed the bigger partition. Image total unchanged: 7,631,744 sectors = 3,907,452,928 bytes.
6. Compressor: `mkfs.erofs -z lz4hc,12` — kernel-fixed config has NO separate LZ4 option (LZ4 decompress always selected with `CONFIG_EROFS_FS_ZIP`, per `fs/erofs/Kconfig`), so any EROFS lz4-file works. Container erofs-utils 1.9.1 does not support zstd/lzma, so the kernel's zstd/lzma options were not usable with the available tool.

## Kernel/DTB (locked, hash-verified by build-image.py)

- kernel `d1d0cc1a…`, DTB `fc723d1f…` — unchanged, reused as-is.

## Subagent reproducibility audit (read-only) — ses_e694cb1d6ffet02YGBIFbTcrGj

Bottom line: enough source to reproduce the fixed kernel+DTB (pristine `linux-6.18.59.tar.xz` + modified in-tree files + configs + documented build flow in `<archive>/research/kernel/BUILD_CONFIG.md`), with gaps:

- No git history in `<archive>/research/kernel/linux-6.18.59/`; four deltas exist only live in-tree and are NOT exported as patches:
  1. `drivers/net/dsa/qca/ar8236.c` + `ar8236.h` (final flow-control/phylink version — `research/ar8236-port/ar8236-modern.c` is a STALE draft missing exactly the flow-control change),
  2. `drivers/mmc/core/block.c` (Cloud LED `ledtrig_disk_activity` — undocumented as diff),
  3. `qca/Kconfig`, `qca/Makefile`, `marvell/Makefile` wiring.
- macOS case-collision damage in tree: ~12 netfilter files collapsed (xt_DSCP/dscp, xt_HL, xt_MARK.h, ipt_ECN.h, ipt_TTL.h, xt_TCPMSS.h, xt_RATEEST.h, xt_CONNMARK.h …) — module rebuilds from the tree can misbuild; pristine tarball is the recovery baseline.
- Toolchain (docker `oc200-kernel-builder`, aarch64 cross toolchain, dtc) not in workspace; no build log for the final fixed Image.
- Recommended follow-up (not done yet): export the four in-tree deltas as patch files + hashes.

## Artifacts (all under <archive>/research/emmc-image/ except rootfs)

| Artifact | Bytes | SHA-256 |
|---|---|---|
| erofs-stage/p1-beta1.erofs | 52,043,776 | 35cf1c79d992748eded533f78aa35c74dcb98dd5acb19f0f141fe99f1fc14ed2 |
| erofs-stage/p2-beta1.f2fs | 3,773,235,200 | 011937d2905f30bfdd130cb9df11df53c869c263dd519ac2dfcf5e0e632beeb2 |
| oc200-alpine-3.24.3-beta1.img | 3,907,452,928 | f3c3a0327511393c2f7418fa8914d38fd0cbc6ec9f51bdca0ec5585e0c9ec01d |

Manifest: `beta1-build-manifest.json` (not kept in this repo; old build metadata, retained only in the local archive).

Verification done: minirootfs tarball hash match; `apk audit` of beta1-rootfs exit 0 (0 missing, 9 U intentional config, 30 A intentional adds); binaries of all 9 beta packages present; `more`/`tload` 4755; fsck.erofs + fsck.f2fs -f clean; labels `rootfs`/`userdata`; MBR written/verified by build-image.py; kernel/DTB hashes match fixed inputs; image size matches DEVICE_SECTORS.

## WebUI recovery pair (follow-up build, same day)

New bootable pair with in-board flash WebUI (user request: "có webui để upload
artifact lên board xử lí phần flash để tiện maintain").

| Artifact | Bytes | SHA-256 |
|---|---|---|
| <archive>/out/oc200-squashfs-initramfs-webui.uImage | 11,632,578 | edef3bcc20a2d59bcaad530ca49ed8149034322e3c5ef95866292763a3534e61 |
| <archive>/out/oc200-alpine-3.24.3-ssh-webui.squashfs | 10,788,864 | 4398d3235853994020067cf170fa1ac9266e11c4d7e4b151a7c02dc2ce56a826 |

Source trees: `squashfs-initramfs/` (wrapper, unchanged) + `ram-tree-extracted/`
(squashfs content) with additions:

- `www/index.html` — uploader UI with Beta-1 LAYOUT constants (mbr LBA0,
  dtb 896, kernel 1024, p1 131072, p2 262144) + auto-fill expected SHA.
- `www/cgi-bin/{common.sh,flash,verify,status,reboot}` — token + rdinit +
  unmounted-partition + CID-optional gates; chunked POST (8 MiB) streamed to
  /dev/mmcblk0 via `head -c` → tmp spool → `dd seek`; sha256 each chunk and
  whole-artifact readback verify. All layout constants in DECIMAL (busybox ash
  cannot parse `0x…` in `$((…))`).
- `etc/oc200-init` — DHCP fallback to `swp1`/`swp2`, token gen
  (16 hex, printed on serial), `busybox-extras httpd -p 80 -h /www`.
- `check_device` in common.sh accepts a regular file only when `T_DEVICE` is
  overridden (test harness); production requires a block device.

Testing done (`tools/webui/test-webui-cgi.sh`, in ARM64 container against a
fake 64 MiB device + `T_*` path overrides): token, RAM-gate, mounted-refusal,
512-alignment, region bounds, raw LBA bound, CID gate, MBR/dtb-pad/chunked
kernel writes + readback SHA, chunk-sha mismatch refusal — all pass. httpd
static + CGI serving smoke-tested.

Not claimed (not tested on real board): httpd behaviour under real concurrent
upload to 512-unaligned eMMC writes via balloon-less dd path, and throughput.

## DTB v2 fix for leds-gpio deferred probe (2026-11-02)

Beta 1 image ships with DTB `fc723d1f…` which has `system-led-pins` and
`system-reset-pins` placed under a fake `south-bridge-pinctrl@18830` node.
The pinctrl subsystem requires pin groups to be children of the actual
pinctrl device (`&pinctrl_sb` = `pinctrl@18800`), so `leds-gpio` and
`gpio-keys` deferred probe forever with "reason unknown".

**Fix:** move `system-led-pins` and `system-reset-pins` into `&pinctrl_sb`
reference. Source: `<archive>/research/kernel/linux-6.18.59/arch/arm64/boot/dts/marvell/armada-3720-oc200.dts`.

| Artifact | Bytes | SHA-256 |
|---|---|---|
| <archive>/out/oc200-6.18.59-swp-ports.dtb (v2) | 12,345 | 9b9955f746ffe2fba8d8f698876af4cedbed0adf1a6f08dd54819d549aeeb42b |

**Verified on board:** after flashing DTB v2 to LBA 896, `/sys/class/leds/system-led`
appears, `/sys/kernel/debug/devices_deferred` is empty, LED blinks on disk I/O
(default trigger `disk-activity`). Cosmetic warning "there is not valid maps
for state default" remains but does not affect functionality.

**Note:** Beta 1 image (`f3c3a032…`) still contains the old DTB. Flash DTB v2
separately after installing the image to get working LEDs. The image hash in
`beta1-build-manifest.json` is unchanged.

**Reset button:** `CONFIG_KEYBOARD_GPIO` is not set in the kernel, so
`gpio-keys` driver is absent. Reset button will not work until kernel is
rebuilt with that config option. Non-critical for Beta testing.

## Full image DTB refresh (2026-11-02)

The distributed full eMMC image was patched in-place at the DTB slot (LBA 896,
64 KiB reserved region) with `oc200-patched.dtb`. The image stayed exactly
3,907,452,928 bytes. Bytes outside LBA 896..1023 were hashed before and after
and matched (`d815315b74c4a52d81ea6819a72be9cb6970e62b11df23066bf8adfd7c295577`).

New full-image SHA-256: `cf15375af5bb529362e4d8cc5b9aabc2fefc80292ba93969ac07dd5dfb2c50cd`.
The WebUI's full-image expected hash was updated in `ram-tree-extracted/www/index.html`,
but the recovery uImage/squashfs still needs a rebuild when the ARM64 Docker
builder is available.

## GPIO-keys kernel rebuild (2026-11-02)

Rebuilt Linux 6.18.59 from the existing OC200 source/config with only
`CONFIG_KEYBOARD_GPIO=y` changed from `n`. The existing DTB v2 remains
unchanged (`oc200-patched.dtb`, `9b9955f7…`). The kernel build was done in a
fresh ARM64 Debian container because the original persistent builder was no
longer present; the old build directory was preserved.

| Artifact | Bytes | SHA-256 |
|---|---:|---|
| `<archive>/out/oc200-linux-6.18.59-Image` | 42,244,608 | `93d2d9ff8ed6f1998383682900cb169700ad653a3184b814f3ac076ecc79a62a` |

The kernel includes `drivers/input/keyboard/gpio_keys.o` built-in. The full
eMMC image kernel slot was replaced at LBA 0x400 and zero-padded to the p1
boundary; bytes outside the kernel slot were unchanged. New full-image SHA-256:
`dd4e3465fac7f3523e31d7cdd368acb342128ade5e28fbac261a2483c75235ea`.

The recovery WebUI pair was rebuilt with the current full-image and kernel
hashes:

- `<archive>/out/oc200-recovery-initramfs.uImage`: `ab286e4f2470f75caa0e5c38a8d65a388c3f8d8ecfbc694ec8fe9aeb77ca0c48`
- `<archive>/out/oc200-recovery.squashfs`: `5b32dfe622b280740348dd70aa42617d068ef46f1108f5b92445c523cfb4edb9`

The recovery uImage's SquashFS was inspected and contains the updated WebUI
hashes.

## Docker bridge/nftables kernel support (2026-11-02)

The Docker daemon error `iptables ... Failed to initialize nft: Protocol not
supported` was traced to Docker networking symbols being modules while the
OC200 rootfs has no matching `/lib/modules/<release>` tree. The host-side
Docker daemon is not using the OC200 kernel; this change is for running
Docker workloads on the OC200 target itself.

The kernel config now builds the Docker bridge/NAT core in (`=y`), including
`BRIDGE_NETFILTER`, `NETFILTER_NETLINK`, `NF_CONNTRACK`, `NF_NAT`,
`NF_TABLES`, `NFT_CT`, `NFT_MASQ`, `NFT_NAT`, `NETFILTER_XTABLES`, IPv4/IPv6
iptables, bridge nftables, conntrack bridge, and `VETH`. `CONFIG_KEYBOARD_GPIO=y`
remains enabled. `NFT_BRIDGE_REJECT` remains a module because it is not needed
for Docker NAT chain creation.

| Artifact | Bytes | SHA-256 |
|---|---:|---|
| `<archive>/out/oc200-linux-6.18.59-Image` | 42,785,280 | `d825afc0203f5b4be4aacb23fbd258e77411295097476a714bce7a5a6dc788c1` |

The kernel was patched into the full image kernel slot (LBA 0x400 to p1),
without changing bytes outside that slot. New full image SHA-256:
`277b2ec1521e651e572fbc9e00b81ff6145a6cf5f87b82efd4012cf90e308a47`.

Recovery pair rebuilt with the new WebUI hashes:

- `<archive>/out/oc200-recovery-initramfs.uImage`: `a33659674619b480922ee2e335c0235169318d587fde4205c595ca2f72315587`
- `<archive>/out/oc200-recovery.squashfs`: `8f10f607aa501ad682845b579cef2bc8ea0f777ec4ee2c5caca1b440b0507578`

## WebUI hash policy (2026-11-02)

Removed all hardcoded artifact SHA-256 values from `www/index.html`. The UI
keeps only target sizes/layout offsets. The expected SHA field is optional:
enter one for strict server-side readback matching, or leave it empty to show
the readback SHA for comparison against the distributed `SHA256SUMS.txt`.
Recovery pair rebuilt and inspected; no 64-hex artifact hashes remain in the
embedded UI.

- `<archive>/out/oc200-recovery-initramfs.uImage`: `d098fd2d129d5d926273cd01f210879ca348a84f72ef72c7ae223d75644767f2`
- `<archive>/out/oc200-recovery.squashfs`: `af077a374d577622fe47782e47c22d73a3fb664461c062d801f114a002d742be`

## Not done (by request)

- Flashing, upload, board boot/acceptance — user handles.

## Beta 2 release cut (2026-11-02)

Beta 2 supersedes the distributed Beta 1 payloads. The existing filenames are
kept stable for tester procedures; the release identity is Beta 2 in
`<archive>/out/build-manifest.json` and the guides. Beta 2 includes the Docker-capable
built-in netfilter/NAT/veth kernel and GPIO-key support. Beta 1 remains the
historical release record and is not overwritten by the Beta 2 label.

## Kernel v8 (2026-11-02, build #8) — config cleanup + USB/sound additions
- full.config: ALL former `=m` symbols converted to explicit `=n` (rootfs has no /lib/modules; they were dead weight). Automated via m→n sweep + curated =y list, deduped (final 10,225 lines, 0 =m).
- Enabled =y on user request: SOUND/SND/SND_USB/SND_USB_AUDIO chain; TUN, WIREGUARD; NFT_REJECT(+INET, NF_REJECT_IPV4/6); FUSE_FS, VFAT_FS (+NLS cp437/iso8859-1/utf8); VLAN_8021Q; USB_USBNET core (root cause when usbnet drivers resolve away); USB net (CDC ether/NCM, AX88179, RTL8152, RTL8150, DM9601, RNDIS, QMI_WWAN, ipheth for iPhone); USB serial (core, FTDI, PL2303, CP210x, CH341, option, qcserial, keyspan, MOS7720/7840, oti6858); USB storage (core, UAS, ISD200, datafab, sddr09, onetouch); USB printer (usblp); USB_MON; BT + RFCOMM(+tty) + BNEP + HCIBTUSB. USB_HID and BT_HIDP explicitly =n (no display on board).
- Image 45,468,160 B (sha 0d488e66…a21b8af2), DTB unchanged 9b9955f7…eeb42b / 12,345 B. Slot tail zero-verified; boots #8.
- Live slot backup (v4 kernel 6d27b7e6…): "/private/var/folders/0q/282vgfds0g5glv14mpmpp8mc0000gp/T/oc200-live-kernel-update-v5"/kernel-slot-before.bin (slot hash a429b621… — first 42,789,376 bytes hash to 6d27b7e6, remainder zero).
- Artifacts updated: out Image, full image sha ef0540f8…, recovery pair (squashfs ffce0011…, uImage 3e92f81a…) with WebUI kernel-size 45468160; manifest/WEBUI_FLASH/GUIDEs hash-synced; kernel-repro config sha e21af03a…; SHA256SUMS.txt regenerated, all OK.
- Docker verified started post-reboot; swp1 static .6 + SLAAC OK. Still pending: swp2 hotplug/ifup-DHCP, mDNS (oc200.local).
