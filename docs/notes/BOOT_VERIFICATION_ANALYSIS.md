# OC200 boot verification analysis

## Dump status (historical report; later read-only captures exist)

The initial capture attempts recorded below failed and left zero-byte
placeholders. A later read-only collection is preserved under
release assets (metadata in `docs/notes/backup-2026-11-01/`): it contains two matching full-size
1 MiB SPI-NOR reads and EOF-sized eMMC user/boot-area reads. The eMMC files are
the actual bytes returned by `dd`, shorter than Linux sysfs logical sizes; they
are not padded whole-device images. See that directory's README and metadata
before using them. RPMB was not accessed. Current build, recovery, and flash
boundaries are in `BETA_BUILD.md`.

- `not-present/research/backups/oc200-nor-1MiB.bin`: 0 bytes
- `not-present/research/backups/oc200-nor-udp.bin`: 0 bytes
- `not-present/research/backups/nc-test.bin`: 0 bytes

The earlier transfer attempts used TCP/UDP through the NAT path and were not reliable. The board did calculate a NOR device hash during one diagnostic session:

```text
56715aafa699c576d11dc36410dfa7854256d5d65293695117c5e6d0b75d5151  /dev/mtd0
```

That is only a digest, not a dump and cannot be used for disassembly or recovery.

## Read-only hardware evidence

The U-Boot session identified:

```text
U-Boot 2017.03-armada-17.10.1
SPI NOR: gd25lq80, 1 MiB
eMMC: Samsung 4FTE4R, 3.6 GiB
```

The read-only eMMC DOS partition map was captured in `research/device-logs/uboot-part-list.txt` and matches the later Linux `fdisk` output:

```text
p1: start 66560,  size 1047552 sectors
p2: start 1114112, size 1024 sectors
p3: start 1115136, size 2048 sectors
p4: start 1117184, size 7200768 sectors
```

No `sf write`, `sf erase`, `mmc write`, `saveenv`, or equivalent write has been executed.

## What the bootloader evidence shows

The captured U-Boot environment contains:

```text
bootcmd=run set_bootargs; run loadfdt; run loadkernel; booti $kernel_addr - $fdt_addr; if test ${booti_result} = failed; then httpd; fi
loadfdt=mmc dev 0; mmc read $fdt_addr 0x380 0x80
loadkernel=mmc dev 0; mmc read $kernel_addr 0x400 0x8000
```

There is no visible `verify`, FIT verification, RSA verification, or signed-image command in this boot path. The board also successfully booted several unsigned custom Linux `Image` files and custom DTBs loaded using:

```text
tftpboot ...
booti ...
```

This is strong evidence that the manually invoked `tftpboot` + `booti` path does not enforce the TP-Link firmware-image signature gate. It is also why the current Alpine/Linux bring-up works without changing NOR or eMMC.

This does not prove that every TP-Link recovery/update path is unsigned. The `httpd` recovery path may have separate image-format and signature checks, and the exact implementation cannot be located without a raw NOR/U-Boot image.

## The RSA-PSS challenge

The userspace `debug` command displayed:

```text
Sign Platform: TP-LINK_SMB_GATEWAY_RSA2048_PSS
```

with per-session challenge strings, and an empty response returned `Password is wrong`. This is evidence of a protected **userspace debug authorization path**. There is no evidence in the captured logs that this challenge is the kernel/U-Boot image boot verifier.

The boot log also contains Marvell/TF-A banners:

```text
WTMI-armada-17.10.3-e96bb83aa
BL1: v1.3(debug):armada-17.10.7:e96bb83aa
BL2: v1.3(debug):armada-17.10.7:e96bb83aa
BL31: v1.3(debug):armada-17.10.7:e96bb83aa
```

The `debug` label in those banners is not proof that signature checks are disabled or enabled; it is only a build/version string.

## Conclusion

A bootloader bypass is not currently necessary for the stated goal. The board already accepts a custom unsigned Linux kernel through the non-persistent UART/U-Boot path, and that path has been used successfully.

A reliable answer about patching or disabling any signed recovery/update path would require a verified 1 MiB NOR dump first. Since no dump exists, there is no basis for identifying offsets, keys, code paths, or safe modifications. No bootloader patch or signature brute force should be attempted.

The safe next step is to obtain a read-only NOR dump with a reliable chunked transfer and verify its size and hash on both sides. Until then, keep using manual `tftpboot` + `booti` and do not write NOR/eMMC.
