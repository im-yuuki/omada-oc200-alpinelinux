# OC200 kernel configuration workflow

The tracked, comment-free configuration snapshot is:

```text
research/kernel/oc200-alpine-full.config
```

The exact resolved `.config` used to build the separately staged F2FS
security-xattr kernel is preserved as
`research/kernel/oc200-6.18.59-resolved.config`. The kernel output itself is
staged in `research/emmc-image/erofs-stage/`; intermediate kernel build
directories are intentionally omitted from the Mac handoff and can be
recreated from source/config if a later kernel rebuild is explicitly needed.

This file is the input configuration for future OC200 kernel builds. It is
derived from the Alpine aarch64 LTS config and then receives the OC200
overrides from `prepare_oc200_alpine_config.py`.

Before each build:

1. Update `research/kernel/oc200-alpine-full.config` first.
2. Copy it to `research/kernel/build-oc200/.config`.
3. Run `olddefconfig`.
4. Build with the persistent builder:

```sh
docker exec oc200-kernel-builder sh -c \
  'make -C /src O=/build ARCH=arm64 \
   CROSS_COMPILE=aarch64-linux-gnu- -j"$(nproc)" Image'
```

The persistent image embeds `/build/erofs-initramfs.cpio` in the kernel. Build
that archive first with `research/alpine/3.24.3/erofs-initramfs/build.sh`, then
run `olddefconfig` and verify `CONFIG_INITRAMFS_SOURCE` names the archive and
`CONFIG_EROFS_FS=y`, `CONFIG_F2FS_FS=y`, `CONFIG_F2FS_FS_SECURITY=y`, and
`CONFIG_LEDS_GPIO=y` before
building. The kernel must remain smaller than the
raw area from LBA `0x400` up to p1 at LBA `0x20000` (maximum `0x1fc00` sectors).
ZRAM is also built in; the persistent OpenRC image activates one 512 MiB
LZO-RLE compressed swap device in volatile RAM.
Armada 37xx CPUFreq is built in for cpufreq sysfs reporting. The OC200 DSA
user-port names are set in the board DTB as `swp1` and `swp2`.
Build the board DTB from `arch/arm64/boot/dts/marvell/armada-3720-oc200.dts`
when changing those labels, then stage the result as
`research/emmc-image/erofs-stage/oc200-6.18.59-swp-ports.dtb`.

The expected release string is:

```text
6.18.59
```

`CONFIG_LOCALVERSION` is empty and `CONFIG_LOCALVERSION_AUTO` is disabled.

The TFTP boot path remains non-persistent. Do not use `saveenv`, `mmc write`,
`sf write`, or `sf erase` during testing.
