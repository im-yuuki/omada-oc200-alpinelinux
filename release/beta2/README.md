# OC200 Alpine Beta 2 artifacts

This folder holds the small Beta 2 release metadata: hashes, manifest, patched
DTB, patched SPI-NOR image, and the kernel reproduction bundle. The large
payloads (eMMC image, partitions, kernel Image, RAM recovery images) are **not**
in git. They are published as release assets of this repository, compressed
with zstd (`*.zst`). Decompress before use, e.g.:

```bash
zstd -d oc200-alpine-3.24.3-full-emmc-image.img.zst
```

Then verify the decompressed file against `SHA256SUMS.txt`. Read both guides
before testing:

1. `docs/guides/GUIDE-RECOVERY.md` — TFTP RAM recovery, installation, component updates,
   and safety.
2. `docs/guides/GUIDE-NORMAL-BOOT.md` — normal eMMC boot and acceptance checks.

Verify payloads against `SHA256SUMS.txt`. It intentionally omits Markdown and
JSON files. The separate `kernel-repro/SHA256SUMS.txt` covers the source/config
bundle files.

## Payloads

| File | Size (bytes) | Purpose |
|---|---:|---|
| `oc200-alpine-3.24.3-full-emmc-image.img` | 3,907,452,928 | Complete eMMC user-area image; WebUI target `raw` |
| `alpine-3.24.3-rootfs.erofs` | 52,043,776 | p1 Alpine root filesystem; WebUI target `p1` |
| `alpine-3.24.3-userdata.f2fs` | 3,773,235,200 | p2 userdata; WebUI target `p2` |
| `oc200-linux-6.18.59-Image` | 42,789,376 | Linux kernel; target `kernel`, also used for TFTP recovery; GPIO keys and Docker networking built in |
| `oc200-patched.dtb` | 12,345 | Current DTB with GPIO LED fix; target `dtb`, also used for TFTP recovery |
| `oc200-recovery-initramfs.uImage` | 11,632,590 | Bootable RAM recovery with WebUI and SSH; TFTP-load at `0x20000000` |
| `oc200-recovery.squashfs` | 10,788,864 | Rootfs embedded in the recovery uImage; included for reference |
| `oc200-spi-nor-bootloader-patched.bin` | 1,048,576 | Full 1 MiB patched SPI-NOR image; **not an eMMC payload** |
| `tftp_compat_server.py` | 3,610 | TFTP server compatible with the OC200's old U-Boot client |
| `kernel-repro/` | — | Kernel/DTB reproducibility bundle; requires separately downloaded upstream Linux source |
| `build-manifest.json` | — | eMMC image layout and component hash record |
| `SHA256SUMS.txt` | — | SHA-256 for payloads/source/script only; excludes JSON and Markdown |

## Important safety notes

- The WebUI writes only `/dev/mmcblk0` (eMMC user area); it does not write
  SPI NOR, eMMC boot partitions, or RPMB.
- `oc200-spi-nor-bootloader-patched.bin` is a separate full-chip SPI-NOR
  artifact. Writing it changes boot firmware and can brick the board if the
  wrong image/tool/offset is used. Do not flash it as part of ordinary Beta
  testing; use only a separately reviewed, board-specific recovery procedure.
- The full eMMC image contains the earlier DTB. After installing it, write
  `oc200-patched.dtb` to the documented DTB slot (LBA 896) from RAM recovery.
- Beta 2's kernel has Docker bridge/NAT/veth and GPIO-key support built in;
  no matching `/lib/modules/<release>` tree is required for those core paths.

The `.img`, `.erofs`, and `.f2fs` files are raw payloads, not external gzip
archives. The recovery uImage contains a gzip-compressed initramfs; SquashFS
uses xz compression internally.

The WebUI does not embed artifact SHA-256 values. Enter an expected SHA-256
manually when strict readback matching is required; leaving it empty performs
readback hashing and displays the result for external comparison against
`SHA256SUMS.txt`.
