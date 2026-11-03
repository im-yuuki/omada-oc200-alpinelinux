# OC200 Alpine SSH squashfs

This is a RAM/TFTP test root filesystem for the OC200. It is not an installation image and does not write NOR or eMMC.

## Artifact

- Root filesystem: `oc200-alpine-3.24.3-ssh.squashfs`
- Format: SquashFS 4.0, XZ-compressed, 128 KiB blocks, no xattrs
- Size: 10,915,840 bytes
- SHA-256: `92057995d63c8265a0085a1c5c11045a9fa25dedf414c69de115786a599f7960`
- TFTP copy: `/var/lib/tftpboot/oc200_test/oc200-alpine-3.24.3-ssh.squashfs`

## Included tools

The Alpine aarch64 root filesystem includes `openssh`, `bash`, `coreutils`, `iperf3`, `iproute2`, `ethtool`, `util-linux`, `e2fsprogs`, `procps`, and BusyBox utilities.

## SSH configuration

- `sshd` listens on IPv4 TCP port 22 on all addresses.
- Root password login is enabled.
- Root password: `alpine`
- `PasswordAuthentication yes`
- `PermitRootLogin yes`
- Host keys are generated under `/run/ssh` at each RAM boot, so they are not persistent.

The image intentionally uses a fixed, weak test password and unrestricted root SSH. Use it only on the isolated test network and change/remove the password before any non-isolated deployment.

## Startup behavior

`/etc/oc200-init` mounts the runtime virtual filesystems, brings up `lan0`, runs BusyBox DHCP, generates temporary SSH host keys, and starts `sshd`. Runtime state is in RAM.

The reduced OC200 kernel configuration enables block-layer, MMC block, DOS partition parsing, SquashFS/XZ, TMPFS and OverlayFS. The final runtime is booted through a RAM-backed OverlayFS wrapper; the board has reached Alpine login and verified the network/tools stack non-persistently.
