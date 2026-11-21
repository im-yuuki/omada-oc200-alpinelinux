# Alpine Linux on the TP-Link OC200

Run Alpine Linux on the TP-Link Omada OC200 as a small ARM64 Linux system.
This is an independent firmware port for the **Marvell Armada 3720 / Atheros
AR8236** board, not an Omada Controller upgrade or an official TP-Link
firmware. Other hardware revisions are not verified.

- **Persistent Alpine:** a read-only EROFS base and writable F2FS overlay on
  eMMC. Configuration and packages installed with `apk` survive reboot.
- **Two Ethernet ports:** `swp1` and `swp2` are exposed by the AR8236 Linux DSA
  driver. They are 10/100 Mbps ports, not Gigabit.
- **Recovery over serial and TFTP:** boot a separate Alpine environment in RAM
  before writing to eMMC. The Beta 2 recovery image includes a flashing Web UI.

## Download or build

**Use the Beta 2 reference build:** Get the eMMC image and recovery files from
[Releases](https://github.com/im-yuuki/omada-oc200-alpinelinux/releases). The
[Beta 2 release notes](release/beta2/README.md) identify the files and their
checksums. Follow the release-specific DTB instructions: the Beta 2 full image
contains an older DTB than the separately published one.

**Build from current source:** Open **Actions → Build OC200 firmware → Run
workflow**. The job selects the latest stable Linux and Alpine releases at run
time, verifies the source archive checksums, and attaches the compressed eMMC
image, kernel, DTB, manifest and checksums to the run. The optional `publish`
checkbox creates a separate GitHub prerelease from the default branch.
See the [CI build guide](docs/guides/GUIDE-CI-BUILD.md) for local builds,
artifact verification and first login. CI images are build-checked, not
hardware-tested; on first serial login you set the root password, and SSH
uses public-key authentication.

## Read before installing

**This is a raw eMMC user-area image. Do not upload it through the stock Omada
firmware updater.** Installing the full image replaces the existing partitions
and erases userdata. Back up your board, verify the image checksum, and confirm
you can boot RAM recovery over serial and TFTP before writing anything.

Start with the [recovery and installation guide](docs/guides/GUIDE-RECOVERY.md),
then use the [normal boot guide](docs/guides/GUIDE-NORMAL-BOOT.md) for the
eMMC boot sequence. These guides describe Beta 2; for CI images, use the
matching build manifest and CI first-login instructions. Stock U-Boot may
need the documented manual boot settings. The separate SPI-NOR image is not
part of normal installation. Do not write SPI-NOR, eMMC boot partitions or
RPMB as part of installing Alpine.

## Development status

On the tested board, TCP receive throughput approaches the 100 Mbps link
limit, but transmit retransmissions and lower throughput remain unresolved.
The current source adds switch counters via `ethtool -S` to help locate the
loss; this is diagnostics, not a throughput fix. See the
[Ethernet investigation](docs/notes/OC200_AR8236_IPERF_INVESTIGATION.md).
When reporting a problem, include the board revision, the build or image hash,
and the relevant serial or network logs. Remove device IDs and credentials
from anything you share.

The [firmware build scripts](build/ci/) fetch upstream inputs instead of
vendoring them. The [board DTS and kernel patches](release/beta2/kernel-repro/)
carry the OC200-specific changes. The board DTS derives from Marvell's Armada
3720 reference work; the AR8236 port draws on TP-Link's vendor GPL sources.
See the source file headers for attribution and licensing. There is no
repository-wide license declaration.
