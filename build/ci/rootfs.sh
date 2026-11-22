#!/bin/bash
set -euo pipefail

# Run as root on a native ARM64 builder so Alpine APK triggers can run in chroot.
here=$(cd -- "$(dirname -- "$0")" && pwd)
archive=$1
branch=$2
work=$3
root="$work/rootfs"
early="$work/initramfs"
mkdir "$root" "$early"
tar -xzf "$archive" -C "$root" --numeric-owner
cp -L /etc/resolv.conf "$root/etc/resolv.conf"
mknod -m 666 "$root/dev/null" c 1 3
mknod -m 666 "$root/dev/urandom" c 1 9
printf 'https://dl-cdn.alpinelinux.org/alpine/%s/%s\n' "$branch" main "$branch" community \
    > "$root/etc/apk/repositories"

target() { chroot "$root" "$@"; }
if ! target /bin/sh -c ': >/dev/null && test -r /dev/urandom'; then
    echo "Build rootfs on a filesystem that permits device nodes (not nodev)" >&2
    exit 1
fi
target /sbin/apk upgrade --no-cache
target /sbin/apk add --no-cache \
    alpine-base busybox-static openssh chrony \
    vim curl tftp-hpa bind-tools tcpdump ethtool strace usbutils f2fs-tools
# Verify the package's account setup without assigning numeric IDs ourselves.
target /bin/busybox id chrony >/dev/null

# Match Beta 2's documented initial login on serial and SSH.
printf '%s\n' 'root:alpine' | target /bin/busybox chpasswd
install -m 755 "$here/zram" "$root/etc/init.d/zram"
sed -i '/^[^#]*::respawn:.*\/getty[[:space:]]/d' "$root/etc/inittab"
printf '\n::respawn:/sbin/getty -L ttyMV0 115200 vt100\n' \
    >> "$root/etc/inittab"
grep -qxF ttyMV0 "$root/etc/securetty" || printf 'ttyMV0\n' >> "$root/etc/securetty"
printf 'oc200\n' > "$root/etc/hostname"
printf '127.0.0.1 localhost localhost.localdomain oc200\n::1 localhost localhost.localdomain\n' \
    > "$root/etc/hosts"
mkdir -p "$root/etc/network" "$root/etc/ssh/sshd_config.d"
cat > "$root/etc/network/interfaces" <<'EOF'
auto lo
iface lo inet loopback

auto swp1
iface swp1 inet dhcp
    use ipv6-ra

auto swp2
iface swp2 inet dhcp
    use ipv6-ra
EOF
printf 'PasswordAuthentication yes\nKbdInteractiveAuthentication no\nPermitRootLogin yes\n' \
    > "$root/etc/ssh/sshd_config.d/00-oc200.conf"
grep -qE '^Include /etc/ssh/sshd_config.d/\*\.conf' "$root/etc/ssh/sshd_config"
target /bin/sh -ec '
    for service in devfs dmesg mdev procfs sysfs; do rc-update add "$service" sysinit; done
    for service in hostname localmount bootmisc seedrng networking zram; do rc-update add "$service" boot; done
    for service in sshd chronyd; do rc-update add "$service" default; done
'

mkdir -p "$early"/{bin,dev,lower,state,newroot}
mknod -m 600 "$early/dev/console" c 5 1
install -m 755 "$here/init" "$early/init"
install -m 755 "$root/bin/busybox.static" "$early/bin/busybox"
target /sbin/apk del --no-cache busybox-static
target /sbin/apk info -v | LC_ALL=C sort > "$work/alpine-packages.txt"
rm "$root/dev/null" "$root/dev/urandom"
: > "$root/etc/resolv.conf"

# Only the init script and a static ARM64 BusyBox go into the kernel.
(cd "$early" && find . -print0 | LC_ALL=C sort -z |
    cpio --null --create --format=newc --owner=0:0 --reproducible --quiet) \
    > "$work/erofs-initramfs.cpio"
