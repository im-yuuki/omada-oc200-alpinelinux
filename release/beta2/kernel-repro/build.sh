#!/bin/sh
# Reproduce the OC200 kernel and DTB from an upstream Linux source tree.
# Usage: ./build.sh /path/to/linux-6.18.59 [output-dir]
set -eu
SRC=${1:?usage: $0 /path/to/linux-6.18.59 [output-dir]}
OUT=${2:-$SRC/.oc200-build}
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
[ -d "$SRC" ] || { echo "source tree not found: $SRC" >&2; exit 1; }
[ -f "$SRC/Makefile" ] || { echo "not a kernel source tree: $SRC" >&2; exit 1; }
mkdir -p "$OUT"

# Apply source deltas. The script is deliberately fail-fast and does not
# overwrite existing patched files: use a fresh output/source copy for each run.
cp "$HERE/drivers/net/dsa/qca/ar8236.c" "$SRC/drivers/net/dsa/qca/ar8236.c"
cp "$HERE/drivers/net/dsa/qca/ar8236.h" "$SRC/drivers/net/dsa/qca/ar8236.h"
patch -p1 -d "$SRC" < "$HERE/patches/0001-ar8236-kconfig.patch"
patch -p1 -d "$SRC" < "$HERE/patches/0002-ar8236-makefile.patch"
patch -p1 -d "$SRC" < "$HERE/patches/0003-mmc-disk-activity-led.patch"
patch -p1 -d "$SRC" < "$HERE/patches/0004-oc200-dtb-makefile.patch"

# The DTS is a complete board DTS but includes the upstream Armada DTSI tree.
cp "$HERE/board-dts/armada-3720-oc200.dts" "$SRC/arch/arm64/boot/dts/marvell/armada-3720-oc200.dts"

# Use the distro's cross compiler by default; override CROSS_COMPILE/ARCH/O.
ARCH=${ARCH:-arm64}
CROSS_COMPILE=${CROSS_COMPILE:-aarch64-linux-gnu-}
cp "$HERE/config/oc200.config" "$OUT/.config"
cp "$HERE/config/erofs-initramfs.cpio" "$OUT/erofs-initramfs.cpio"
sed -i "s#/build/erofs-initramfs.cpio#$OUT/erofs-initramfs.cpio#g" "$OUT/.config"
make -C "$SRC" O="$OUT" ARCH="$ARCH" CROSS_COMPILE="$CROSS_COMPILE" olddefconfig
make -C "$SRC" O="$OUT" ARCH="$ARCH" CROSS_COMPILE="$CROSS_COMPILE" -j"${JOBS:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 2)}" Image dtbs
[ -f "$OUT/arch/arm64/boot/dts/marvell/armada-3720-oc200.dtb" ] || {
	echo "OC200 DTB was not produced" >&2
	exit 1
}

printf '%s\n' "Kernel: $OUT/arch/arm64/boot/Image"
printf '%s\n' "DTB:    $OUT/arch/arm64/boot/dts/marvell/armada-3720-oc200.dtb"
