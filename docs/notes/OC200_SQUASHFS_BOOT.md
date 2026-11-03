# Non-persistent OC200 SquashFS boot

The updated kernel is:

`oc200_test/oc200-6.18.59-alpine-full-usb-Image`

The initramfs wrapper is:

`oc200_test/oc200-squashfs-initramfs.uImage`

The wrapper contains BusyBox and the SquashFS image. It mounts the SquashFS as a lower layer, creates a RAM-backed tmpfs upper/work layer, mounts OverlayFS as the writable root, moves runtime mounts into it, and calls `switch_root`; the final root starts DHCP on `lan0` and OpenSSH on TCP port 22. The unsupported `UsePAM` directive is removed from the SSH configuration.

Latest wrapper SHA-256:

```text
4e78cc46a733a6d721749c022646c114694666b9e88634074b1f5a71b6d04424
```

From the U-Boot prompt, use these commands without saving the environment:

```text
setenv ipaddr 192.168.12.240
setenv serverip 10.255.255.1
setenv gatewayip 192.168.12.1
setenv netmask 255.255.255.0
ping 10.255.255.1
tftpboot 0x05000000 oc200_test/oc200-6.18.59-alpine-full-usb-Image
setenv initrd_size 0
tftpboot 0x20000000 oc200_test/oc200-squashfs-initramfs.uImage
setenv initrd_size ${filesize}
tftpboot 0x04f00000 oc200_test/oc200-6.18.59-swp-ports.dtb
setenv bootargs console=ttyMV0,115200 earlycon=ar3700_uart,0xd0012000 rdinit=/init loglevel=7
booti 0x05000000 0x20000000:${initrd_size} 0x04f00000
```

Stage the current DTB from `[đã xóa] <archive>/research/emmc-image/erofs-stage/` as shown; its
SHA-256 and current TFTP/recovery guidance are in `BETA_BUILD.md`. Do not run
`saveenv`, `mmc write`, or `sf write`.

The final root is intended to be reached at the DHCP lease address. Login:

```text
ssh root@<lease-address>
password: alpine
```
