# OC200 SPI-NOR static analysis

Analysis source: `<archive>/research/backups/oc200-emmc-2026-11-01/oc200-spi-nor-1MiB.bin`.
Read-only static inspection only; no code was executed and no hardware was
accessed during this analysis.

## Integrity and identity

- Device: GigaDevice GD25LQ80, 1 MiB, 4 KiB erase sectors.
- Two consecutive `/dev/mtd0ro` reads compared byte-for-byte equal.
- SHA-256: `56715aafa699c576d11dc36410dfa7854256d5d65293695117c5e6d0b75d5151`.

## Findings

- A Marvell TIM-like header begins at offset `0x4` (`TIMH`). Nonzero regions
  at `0x000000–0x006fff` and `0x015000–0x019fff` are consistent with boot-chain
  images, but exact component boundaries have not been validated.
- U-Boot strings/defaults are present around `0x0b0000–0x0cffff`. Version
  string at `0x0b5497` identifies `2017.03-armada-17.10.1`, built 2024-01-25.
- Compiled defaults near `0x0a3660` include `bootcmd`, `loadfdt`, `loadkernel`
  and HTTP recovery behavior. Separate environment capture indicates the
  `burnfdt`, `burnkernel` and `burnrootfs` commands target eMMC. This does not
  establish that a valid persisted environment currently exists in NOR.
- Strings and key-format material around `0x0b6cd5–0x0b7259` are consistent
  with firmware RSA verification and an embedded verification/public key.
  No evidence of a signing private key or PEM private-key material was found.
- A structured device-specific record begins at `0x0e0000`. It contains
  identity/authentication fields; exact values are intentionally omitted here.
  Do not publish or reuse them.
- Some NOR ranges are all-zero or erased (`0xff`), but that is not proof they
  are safe to repurpose. The observed `mtdparts` definition describes the full
  1 MiB as one device and does not validate internal component boundaries.

## Practical conclusion

The dump is valuable for offline bootloader analysis and preservation. It does
not provide a demonstrated, validated full-chip restore procedure. The
embedded verification key can help identify/verify signed firmware; it does
not enable creating a valid vendor signature. Keep the original dump and hash
unchanged, and do not write it back without format-aware validation and a
recovery plan.
