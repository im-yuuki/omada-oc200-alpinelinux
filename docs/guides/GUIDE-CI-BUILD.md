# Build OC200 firmware in GitHub Actions

On the repository's **Actions** tab, select **Build OC200 firmware → Run
workflow**. The workflow is manual-only. The firmware is always attached to
the run as an artifact. Tick **Upload to GitHub Releases as a new prerelease**
to publish the same files under a run-specific tag. Publishing works only
from the default branch and never replaces Beta 2 or marks the build as the
latest GitHub Release.

## Inputs and outputs

The ARM64 runner resolves the latest stable (non-RC) upstream Linux release
from kernel.org and the latest stable Alpine aarch64 minirootfs from Alpine's
release metadata. It checks SHA-256 before using either archive, including on
cache hits. The download cache is keyed by upstream digests; a separate
`ccache` directory caches kernel compilation. Alpine packages are installed
fresh from the resolved stable branch with APK signature checks and triggers
enabled. Their versions, the resolved kernel config, source revision and
payload hashes accompany each image.

The build applies the OC200 patches from the repository, including AR8236
MIB diagnostics. Compatibility with future Linux releases is not guaranteed:
the workflow fails instead of silently skipping patches, switching kernels,
or packaging a DTB, kernel or rootfs that exceeds the Beta 2 boot/partition
limits. To adapt to an upstream change, use the error and resolved version in
the run log.

The main artifact is `oc200-alpine-*-linux-*-emmc.img.zst`, a compressed eMMC
**user-area** image with an embedded persistent initramfs, EROFS `rootfs` and
F2FS `userdata` (OverlayFS). The two backing filesystems are mounted in the
initramfs and do not require dedicated mount points in the Alpine root.
The archive also contains a separate compressed
ARM64 `Image`, `oc200.dtb`, `kernel.config`, `alpine-packages.txt`,
`build-manifest.json` and `SHA256SUMS.txt`. Check the downloaded files with
`sha256sum -c SHA256SUMS.txt`; the manifest also records the raw image hash.

## Boot and safety

These builds are not board-tested or a vendor firmware update package. The
full eMMC image replaces both existing partitions, including userdata. It
contains no SPI-NOR, bootloader, eMMC boot partition, or RAM recovery image.
Read [the recovery guide](GUIDE-RECOVERY.md) before any device operation.

On a fresh image, log in as `root` with password `alpine` via serial
(`ttyMV0`, 115200 8N1) or SSH. Change the default password with `passwd`
before connecting the board to an untrusted network. SSH host keys are
generated on the device, not included in the image. Both `swp` ports are
configured for DHCP and SLAAC. Docker userspace is not bundled.

Fresh images leave the `chrony` user, group and data permissions to the
Alpine package; the build does not pin numeric IDs. A kernel-only update
does not change accounts or data in the installed rootfs. Direct serial and
SSH login and the original 512 MiB `/dev/zram0` service are retained from
Beta 2. These builds do not use eMMC for swap.

## Run locally

On a native ARM64 Ubuntu 24.04 host, install the tools listed in
[`firmware.yml`](../../.github/workflows/firmware.yml), then:

```sh
work=$(mktemp -d "$HOME/oc200-build.XXXXXX")
python3 build/ci/upstream.py "$work/upstream.json"
python3 build/ci/upstream.py "$work/upstream.json" --fetch "$work/downloads"
bash build/ci/firmware.sh "$work/upstream.json" "$work/downloads" "$work/build" "$work/dist"
```

The scripts use `sudo` to prepare the chroot and preserve rootfs ownership
and permissions. Use a build filesystem that permits device nodes for the
chroot, not a `nodev` mount. The scripts assemble image files only and never
write to a board or a block device.
