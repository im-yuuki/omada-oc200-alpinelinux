# OC200 bootloader / SPI-NOR intervention record

Date: 2026-11-01  
Target: TP-Link OC200 at `192.168.12.27` (SSH as root, public-key authentication)  
Status: SPI-NOR patch written, readback-verified, and board reboot reported successful by the user.

## Scope and intent

The built-in U-Boot default environment was patched offline to boot the Beta 1
kernel and DTB from the eMMC user area, retaining the original `httpd` recovery
fallback if `booti` reports failure. The patch was applied to the 1 MiB SPI NOR
on the board. This was a separate bootloader intervention, not part of the
Beta 1 eMMC image write.

Only two 4 KiB erase sectors were intended to change:

| NOR offset | Contents | Final candidate sector SHA-256 |
|---:|---|---|
| `0x000000` | TIM descriptors and updated hashes | `312ca364dc5dfc210807a4f45d51e87e0e86519417b179481f11d2549bb29ca9` |
| `0x00a3000` | Embedded U-Boot default environment and padding | `562ce790d64daf6bbfd0a139d9073bed31797ac50a416a024bfb24fecb09f865` |

The environment starts at `0xa3660`. The patched environment keeps the
original `bootcmd` recovery fallback and uses the eMMC read windows documented
in `docs/guides/GUIDE-NORMAL-BOOT.md`. The candidate replaces unneeded defaults with an
inert `zz_patch_pad` record so the environment span and double-NUL terminator
remain in place. The TIM IMBO hash and the dependent first TIM-image hash were
updated in the candidate.

The environment occupies the fixed range `[0xa3660, 0xa3aa9)` (1,097 bytes).
The operative entries are:

```text
bootcmd=run set_bootargs; run loadfdt; run loadkernel; booti $kernel_addr - $fdt_addr; if test ${booti_result} = failed; then httpd; fi
bootdelay=1
baudrate=115200
fdt_addr=0x4f00000
fdt_high=0xffffffffffffffff
kernel_addr=0x5000000
console=console=ttyMV0,115200 earlycon=ar3700_uart,0xd0012000
root=root=/dev/mmcblk0p1 rw rootwait
set_bootargs=setenv bootargs $console $root
loadfdt=mmc dev 0; mmc read $fdt_addr 0x380 0x19
loadkernel=mmc dev 0; mmc read $kernel_addr 0x400 0x1fc00
```

The default pad record is `zz_patch_pad=...`; its zero-filled value is inert
and ends before the preserved double-NUL terminator. Other removed defaults
and the full byte-level diff are listed in
`<archive>/out/OC200_NORMAL_ENV_PATCH_REPORT.md` and
`<archive>/out/OC200_NORMAL_BOOT_SPI_NOR_DIFF_MANIFEST.md`.

## Pre-write checks

The board reported MTD NOR geometry:

```text
device:    /dev/mtd0 (read-only source: /dev/mtd0ro)
size:      1,048,576 bytes (0x100000)
erase size: 4,096 bytes (0x1000)
type:      NOR
```

A complete read of `/dev/mtd0ro` returned 1,048,576 bytes and SHA-256
`56715aafa699c576d11dc36410dfa7854256d5d65293695117c5e6d0b75d5151`, matching
the preserved original dump. The later sector-level prechecks were not a valid
confirmation of the original sector contents: they were run after erase
commands had already occurred. `mtd-utils` was installed on the running system
to obtain `flash_erase`, `flashcp`, and `mtd_debug`.

## Write incident and recovery

The first `scp` command did not leave the sector images at the paths expected
on the board. Despite that, erase commands were run for offsets `0xa3000` and
`0x000000`; subsequent `dd` writes failed because their input files were
missing. Thus, at that point, both sectors had been erased and not restored.

After copying the files individually and verifying their sizes and hashes, a
further mistake was made: `flashcp` was used as though it accepted a destination
offset. It does not; it operates from the start of the MTD device. The
`0xa3000` sector image was therefore sent to offset zero, and the environment
sector at `0xa3000` remained erased. The offset-zero sector was subsequently
rewritten with its correct candidate image, but the full-chip hash exposed
that the environment sector still needed restoration.

Recovery used `mtd_debug write`, which accepts an explicit flash offset:

```text
/usr/sbin/mtd_debug write /dev/mtd0 0xa3000 4096 /tmp/sector-0a3000.bin
```

The sector was read back and its SHA-256 matched the candidate. No reboot or
power cycle was performed during recovery. A complete 1 MiB read of the NOR
then matched the candidate's full-chip SHA-256 exactly.

## Final verification and outcome

Immediately before the user authorized reboot, the final full-device read
reported:

```text
read size: 1,048,576 bytes
SHA-256:   3e0da3d8051f3934fead5f8d43782c3b4ad3c3edb851288ece42697be1f4ec18
result:    PASS (matches the offline candidate)
```

The user subsequently reported that the board rebooted successfully. A later
post-reboot SSH read of all 1,048,576 bytes from `/dev/mtd0ro` independently
returned the same candidate SHA-256 above (`3e0da3d8…be1f4ec18`). No serial boot
log was collected. Do not infer from the reboot and NOR hash alone that every
Linux runtime acceptance test has passed.

The targeted sector images and full candidate remain at:

```text
<archive>/out/oc200-spi-nor-normal-boot-diff-sectors/sector-000000.bin
<archive>/out/oc200-spi-nor-normal-boot-diff-sectors/sector-0a3000.bin
<archive>/out/oc200-spi-nor-normal-boot-env-patched.bin
<archive>/out/OC200_NORMAL_BOOT_SPI_NOR_DIFF_MANIFEST.json
<archive>/out/OC200_NORMAL_BOOT_SPI_NOR_DIFF_MANIFEST.md
```

Candidate full-image SHA-256:

```text
3e0da3d8051f3934fead5f8d43782c3b4ad3c3edb851288ece42697be1f4ec18
```

The original NOR dump remains the rollback/reference artifact and must not be
overwritten:

```text
<archive>/research/backups/oc200-emmc-2026-11-01/oc200-spi-nor-1MiB.bin
56715aafa699c576d11dc36410dfa7854256d5d65293695117c5e6d0b75d5151
```

## Operational notes

- The user supplied stable power from a power bank. Preserve stable power and
  do not interrupt the board during future NOR operations.
- Do not use `flashcp` for offset writes. Use an offset-aware MTD operation and
  confirm its syntax before any future write.
- The NOR is now intentionally different from its original backup. Compare it
  to the candidate hash above, not the original hash.
- The user-space installation of `mtd-utils` was performed on the board to
  enable the operation; this record does not claim that package was removed.
- Keep serial recovery / external programmer options available. The successful
  reboot is encouraging but does not establish a tested full-chip restore path.
- Never publish board identity/authentication data from the NOR dump.
