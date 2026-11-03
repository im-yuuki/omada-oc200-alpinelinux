# OC200 eMMC read-only dump

Collected over SSH from the live RAM/TFTP Alpine system at `192.168.12.26`.
The board-side command was read-only `dd`; no write, erase, or boot-partition
configuration command was issued.

## Files

```text
user.img   SHA-256 5f98143c8f95874499a22a2b474a6229b8b5ec33de0d93d1bdc54ef13a3d39ad
boot0.img  SHA-256 2dce229d0099e7e3cc65ee98b92893c8618bb92ccec498c09ade0da8ff28a9df
boot1.img  SHA-256 f9f69f2afaff3dbcd98b095f62d13b5cfcd9eb032d5c4dee39cab25912abc9ac
```

The actual byte counts returned by the device were:

```text
user.img:  3,907,452,928 bytes
boot0.img:     2,359,296 bytes
boot1.img:     2,293,760 bytes
```

The Linux sysfs logical sizes reported before the read were 3,909,091,328
bytes for the user area and 4,194,304 bytes for each boot area. `dd` reached
EOF earlier for all three regions. The files preserve the bytes actually
returned by the eMMC and are not padded or synthesized. The board-side `sha256sum`
results match the files after zero-padding each file to the corresponding Linux
logical size; see `checksum-comparison.json`.

## Device identity

```text
name:   4FTE4R
manfid: 0x000015
oemid:  0x0100
serial: 0xc013bc16
CID:    15010034465445345201c013bc16b600
CSD:    d02701320f5903fff6dbffef8e404000
```

The user image contains a valid DOS/MBR signature and the observed partition
entries:

```text
p1: start 66560,   size 1047552 sectors
p2: start 1114112, size 1024 sectors
p3: start 1115136, size 2048 sectors
p4: start 1117184, size 7200768 sectors
```

The p4 entry extends past the number of sectors that `dd` actually returned;
this matches the existing kernel warning that p4 is truncated at end of
device. Do not use the dump as a complete padded disk image without explicitly
preserving this size discrepancy.

## Boot areas

`mmcblk0boot0` and `mmcblk0boot1` were both reported read-only by sysfs. Their
dump files are partial EOF-sized reads, not writes or reconstructed images.
RPMB was not accessed; it is a protected eMMC area and is intentionally not
part of this dump.
