# OC200 Alpine

Notes, build scripts, and release metadata for running Alpine Linux on the
OC200 (Armada 3720) board.

## Layout

| Path | Contents |
|---|---|
| `docs/` | Beta build notes, user guides (`docs/guides/`), investigation notes (`docs/notes/`) |
| `build/kernel/` | Kernel configs, config history, DTS, config-prep script |
| `build/emmc-image/` | eMMC image build scripts, partition layouts, flash helper, readback logs |
| `release/beta2/` | Release metadata: SHA256 sums, manifest, patched DTB and SPI-NOR image, kernel reproduction bundle |
| `tools/` | Serial/RFC2217 helper scripts for the board, TFTP recovery server, backup receiver, WebUI test harness (`tools/webui/`) |
| `research/` | Device U-Boot logs, boot log, AR8236 switch driver notes/code |

## Not in this repository

Large binaries are kept outside git and published as release assets: eMMC
images and partitions, kernel and recovery images, device backups, and the
upstream Linux and vendor GPL source tarballs. Compressed payloads are `.zst`;
decompress with `zstd -d`. Their hashes are in `release/beta2/SHA256SUMS.txt`
(payloads) and `release/beta2/RELEASE-ASSETS-SHA256SUMS.txt` (uploaded assets).

The kernel source tree is not stored. Rebuild it from `linux-6.18.59.tar.xz`
and apply `release/beta2/kernel-repro/patches`.

Paths marked `not-retained/` were Beta 1 intermediate inputs and are not kept.
Paths marked `not-present/` never existed in the working tree.

## Start here

1. `docs/BETA_BUILD.md`
2. `docs/guides/GUIDE-RECOVERY.md` before any flashing
3. `docs/guides/GUIDE-NORMAL-BOOT.md` for acceptance checks
