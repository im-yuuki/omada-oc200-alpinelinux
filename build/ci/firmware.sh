#!/bin/bash
set -euo pipefail

here=$(cd -- "$(dirname -- "$0")" && pwd)
repo=$(git -C "$here" rev-parse --show-toplevel)
bundle="$repo/release/beta2/kernel-repro"
upstream=$(realpath "$1")
downloads=$(realpath "$2")
work=$(realpath -m "$3")
dist=$(realpath -m "$4")
linux=$(jq -r .linux.version "$upstream")
alpine=$(jq -r .alpine.version "$upstream")
branch=$(jq -r .alpine.branch "$upstream")
name="oc200-alpine-$alpine-linux-$linux"
src="$work/linux-$linux"
out="$work/kbuild"
jobs=${JOBS:-$(nproc)}

[ "$(uname -m)" = aarch64 ] || { echo "Use a native ARM64 runner" >&2; exit 1; }
mkdir "$work" "$dist"
sudo bash "$here/rootfs.sh" "$downloads/$(jq -r .alpine.file "$upstream")" "$branch" "$work"

tar -xJf "$downloads/$(jq -r .linux.file "$upstream")" -C "$work"
cp "$bundle/drivers/net/dsa/qca/"ar8236.[ch] "$src/drivers/net/dsa/qca/"
cp "$bundle/board-dts/armada-3720-oc200.dts" "$src/arch/arm64/boot/dts/marvell/"
for patch in "$bundle"/patches/*.patch; do
    patch --batch --forward --fuzz=0 -p1 -d "$src" < "$patch" ||
        { echo "OC200 patch $(basename "$patch") needs updating for Linux $linux" >&2; exit 1; }
done

mkdir "$out"
cp "$bundle/config/oc200.config" "$out/.config"
"$src/scripts/config" --file "$out/.config" --set-str INITRAMFS_SOURCE "$work/erofs-initramfs.cpio"
kmake=(make -C "$src" O="$out" ARCH=arm64 "CC=${KERNEL_CC:-ccache gcc}")
"${kmake[@]}" olddefconfig
for symbol in ARCH_MVEBU BLK_DEV_INITRD DEVTMPFS MSDOS_PARTITION PROC_FS SYSFS TMPFS IPV6 \
    MMC MMC_BLOCK MMC_SDHCI MMC_SDHCI_PLTFM MMC_SDHCI_XENON EROFS_FS EROFS_FS_ZIP \
    F2FS_FS F2FS_FS_XATTR F2FS_FS_SECURITY OVERLAY_FS ZRAM ZRAM_BACKEND_LZO \
    NET_DSA_AR8236 NET_DSA_TAG_QCA MVNETA MVMDIO SERIAL_MVEBU_CONSOLE \
    LEDS_GPIO LEDS_TRIGGER_DISK KEYBOARD_GPIO; do
    grep -qx "CONFIG_$symbol=y" "$out/.config" ||
        { echo "Linux $linux is missing required CONFIG_$symbol=y" >&2; exit 1; }
done
grep -Fxq "CONFIG_INITRAMFS_SOURCE=\"$work/erofs-initramfs.cpio\"" "$out/.config"
"${kmake[@]}" -j"$jobs" Image marvell/armada-3720-oc200.dtb

sudo mkfs.erofs -z lz4hc,12 -L rootfs "$work/rootfs.erofs" "$work/rootfs" | tail -12
truncate -s "$(( (7631744 - 262144) * 512 ))" "$work/userdata.f2fs"
mkfs.f2fs -f -l userdata "$work/userdata.f2fs"
fsck.erofs "$work/rootfs.erofs"
fsck.f2fs -f "$work/userdata.f2fs"
check_fs() {
    local path=$1 type=$2 label=$3
    if [[ "$(blkid -p -s TYPE -o value "$path")" != "$type" ||
          "$(blkid -p -s LABEL -o value "$path")" != "$label" ]]; then
        echo "Unexpected filesystem type/label for $path (expected $type/$label)" >&2
        exit 1
    fi
}
check_fs "$work/rootfs.erofs" erofs rootfs
check_fs "$work/userdata.f2fs" f2fs userdata

image="$work/$name-emmc.img"
kernel="$out/arch/arm64/boot/Image"
dtb="$out/arch/arm64/boot/dts/marvell/armada-3720-oc200.dtb"
cp "$dtb" "$dist/oc200.dtb"
cp "$out/.config" "$dist/kernel.config"
cp "$work/alpine-packages.txt" "$dist/alpine-packages.txt"
python3 "$here/image.py" --commit "$(git -C "$repo" rev-parse HEAD)" --upstream "$upstream" \
    --dtb "$dtb" --kernel "$kernel" --rootfs "$work/rootfs.erofs" \
    --userdata "$work/userdata.f2fs" --image "$image" --manifest "$dist/build-manifest.json"

zstd -T2 -10 "$image" -o "$dist/$(basename "$image").zst"
zstd -T2 -10 "$kernel" -o "$dist/oc200-linux-$linux-Image.zst"
zstd -t "$dist/"*.zst
for asset in "$dist/"*; do
    [ "$(stat -c %s "$asset")" -lt "$((2 * 1024 * 1024 * 1024))" ] ||
        { echo "Asset exceeds GitHub's size limit: $asset" >&2; exit 1; }
done
(cd "$dist" && sha256sum ./* > "$work/SHA256SUMS.txt")
mv "$work/SHA256SUMS.txt" "$dist/"
