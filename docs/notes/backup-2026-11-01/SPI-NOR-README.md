# OC200 SPI-NOR read-only backup

Captured over SSH from Alpine 3.24.3 running entirely in RAM/TFTP on
2026-11-01. Read source was `/dev/mtd0ro`; no NOR erase, program, or
environment-save operation was issued.

Device: GigaDevice GD25LQ80, 1 MiB (`/proc/mtd`: `mtd0`, size `00100000`,
erase size `00001000`).

Files:

```text
oc200-spi-nor-1MiB.bin        1,048,576 bytes
oc200-spi-nor-1MiB-read1.bin  1,048,576 bytes
oc200-spi-nor-1MiB-read2.bin  1,048,576 bytes
```

SHA-256 for all three (two consecutive independent reads):

```text
56715aafa699c576d11dc36410dfa7854256d5d65293695117c5e6d0b75d5151
```

The two raw reads compared byte-for-byte equal. These files remain the
read-only original backups and have not been modified. A later intervention
changed two sectors on the board; the NOR no longer matches this original dump.
See `research/OC200_BOOTLOADER_INTERVENTION_2026-11-01.md` for the patched
candidate hash, readback verification, and incident record. The successful
intervention does not establish a tested full-chip restore path.
