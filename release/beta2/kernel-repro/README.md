# OC200 kernel reproducibility bundle

This directory contains the board-specific source/configuration needed to
rebuild the OC200 kernel and DTB. It intentionally does **not** contain the
Linux kernel source. Download the exact upstream source separately:

- Version: Linux `6.18.59`
- Expected source archive: `linux-6.18.59.tar.xz`
- Expected archive SHA-256: verify against the official upstream release
  before extraction; do not use a vendor-patched tree.

## Contents

```text
build.sh                         reproducible build entry point
config/oc200.config              resolved OC200 config, current release config
config/erofs-initramfs.cpio      embedded persistent initramfs input referenced by the config
board-dts/armada-3720-oc200.dts  OC200 board DTS, including GPIO LED/reset nodes
drivers/net/dsa/qca/ar8236.c     OC200 AR8236 DSA driver
drivers/net/dsa/qca/ar8236.h     AR8236 driver definitions
patches/0001-ar8236-kconfig.patch
patches/0002-ar8236-makefile.patch
patches/0003-mmc-disk-activity-led.patch
patches/0004-oc200-dtb-makefile.patch
```

The DTS includes upstream files from:

```text
arch/arm64/boot/dts/marvell/armada-372x.dtsi
arch/arm64/boot/dts/marvell/armada-37xx.dtsi
include/dt-bindings/gpio/gpio.h
include/dt-bindings/input/input.h
```

Keep the upstream source on a case-sensitive filesystem. macOS default
case-insensitive filesystems can collapse Linux paths whose names differ only
by case and make a rebuild unreliable.

## Build on ARM64 Alpine/Debian/container

Install an ARM64 cross compiler, `make`, `bc`, `bison`, `flex`, `dtc`, `cpio`,
OpenSSL development tools, and the usual kernel build dependencies. Then:

```sh
cd /path/to/oc200-alpine/release/beta2/kernel-repro
./build.sh /path/to/linux-6.18.59 /tmp/oc200-build
```

For Debian/Ubuntu:

```sh
sudo apt install bc bison build-essential cpio device-tree-compiler flex \
  gcc-aarch64-linux-gnu libelf-dev libssl-dev openssl perl python3 rsync
```

The DTS build is reproducible byte-for-byte: the current source/config/toolchain test produced DTB SHA-256 `9b9955f746ffe2fba8d8f698876af4cedbed0adf1a6f08dd54819d549aeeb42b` and size 12,345 bytes. The kernel Image can differ if compiler version, build metadata, or timestamps differ; verify its config and boot/test it rather than requiring a byte-identical Image.

The script outputs:

```text
/tmp/oc200-build/arch/arm64/boot/Image
/tmp/oc200-build/arch/arm64/boot/dts/marvell/armada-3720-oc200.dtb
```

Expected config essentials for this release include:

```text
CONFIG_KEYBOARD_GPIO=y
CONFIG_LEDS_GPIO=y
CONFIG_NET_DSA_AR8236=y
CONFIG_BRIDGE_NETFILTER=y
CONFIG_NF_TABLES=y
CONFIG_NF_NAT=y
CONFIG_NFT_CT=y
CONFIG_NFT_MASQ=y
CONFIG_NFT_NAT=y
CONFIG_VETH=y
```

The Docker networking symbols are built-in because this target image does
not ship a matching `/lib/modules/<release>` tree. The GPIO reset button is
built-in for the same reason.

## Changes represented here

- Adds the Qualcomm/Atheros AR8236 DSA driver and Kconfig/Makefile wiring.
- Adds MMC block I/O calls to the disk-activity LED trigger.
- Provides the OC200 board DTS: AR8236 DSA ports (`swp1`/`swp2`), eMMC,
  GPIO Cloud/system LED on `gpiosb` pin 1, reset button on `gpiosb` pin 4,
  USB, and the corrected `&pinctrl_sb` pin groups.
- Carries the release kernel configuration, including GPIO keys and built-in
  Docker bridge/nftables/NAT support.

Do not flash a rebuilt kernel or DTB without checking its size against the
OC200 eMMC layout. Kernel payloads start at LBA `0x400`; p1 starts at
`0x20000`. DTB payloads start at LBA `0x380`.
