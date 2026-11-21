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
F2FS `userdata` (OverlayFS). The archive also contains a separate compressed
ARM64 `Image`, `oc200.dtb`, `kernel.config`, `alpine-packages.txt`,
`build-manifest.json` and `SHA256SUMS.txt`. Check the downloaded files with
`sha256sum -c SHA256SUMS.txt`; the manifest also records the raw image hash.

## Boot and safety

These builds are not board-tested or a vendor firmware update package. The
full eMMC image replaces both existing partitions, including userdata. It
contains no SPI-NOR, bootloader, eMMC boot partition, or RAM recovery image.
Read [the recovery guide](GUIDE-RECOVERY.md) before any device operation.

There is no shared root password or baked-in SSH host key. On first serial
login (`ttyMV0`, 115200 8N1), set a root password at the prompt. For SSH,
install a public key in `/root/.ssh/authorized_keys` from the serial console;
password authentication over SSH is disabled. Both `swp` ports are configured
for DHCP and SLAAC. Docker userspace is not bundled.

## Run locally

On a native ARM64 Ubuntu 24.04 host, install the tools listed in
[`firmware.yml`](../../.github/workflows/firmware.yml), then:

```sh
work=$(mktemp -d)
python3 build/ci/upstream.py "$work/upstream.json"
python3 build/ci/upstream.py "$work/upstream.json" --fetch "$work/downloads"
bash build/ci/firmware.sh "$work/upstream.json" "$work/downloads" "$work/build" "$work/dist"
```

The scripts use `sudo` to prepare the chroot and preserve rootfs ownership
and permissions. They assemble regular files only and never write to a device.
