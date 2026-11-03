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
| `tools/` | Serial/RFC2217 helper scripts for the board, TFTP recovery server, backup receiver |
| `research/` | Device U-Boot logs, boot log, AR8236 switch driver notes/code |
| `.private/beta1/` | Beta 1 build notes and audit output |

## Not in this repository

Large binaries (eMMC images, partitions, kernel/rootfs images, device dumps,
upstream Linux and OpenWrt source trees, and build directories) are kept
outside git. Their hashes are listed in `release/beta2/SHA256SUMS.txt` and
`release/beta2/build-manifest.json`.

## Start here

1. `docs/BETA_BUILD.md`
2. `docs/guides/GUIDE-RECOVERY.md` before any flashing
3. `docs/guides/GUIDE-NORMAL-BOOT.md` for acceptance checks
