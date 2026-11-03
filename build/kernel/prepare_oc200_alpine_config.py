#!/usr/bin/env python3
"""Prepare an Alpine-compatible OC200 kernel config.

Start with Alpine's shipped aarch64 config, then force the OC200 bring-up
drivers and filesystems built-in.  The stock Alpine config is intentionally
kept as the broad userspace/runtime baseline; the overrides below are the
board-specific exceptions needed when no module tree is shipped in the TFTP
initramfs.
"""

from __future__ import annotations

import argparse
from pathlib import Path


FORCE_BUILTIN = {
    # Keep the uname release stable and do not require an Alpine signing key
    # path that only exists in the Alpine build farm.
    "CONFIG_LOCALVERSION": '""',
    "CONFIG_LOCALVERSION_AUTO": "n",
    "CONFIG_MODULE_SIG": "n",
    "CONFIG_MODULE_SIG_ALL": "n",
    "CONFIG_MODULE_SIG_KEY": '""',
    # Filesystems and userspace runtime.
    "CONFIG_BLOCK": "y",
    "CONFIG_BLK_DEV_LOOP": "y",
    "CONFIG_FILE_LOCKING": "y",
    "CONFIG_BLK_DEV_INITRD": "y",
    "CONFIG_INITRAMFS_SOURCE": '"/build/erofs-initramfs.cpio"',
    "CONFIG_EXT4_FS": "y",
    "CONFIG_F2FS_FS": "y",
    "CONFIG_ZRAM": "y",
    "CONFIG_EROFS_FS": "y",
    "CONFIG_SQUASHFS": "y",
    "CONFIG_SQUASHFS_XZ": "y",
    "CONFIG_SQUASHFS_FILE_CACHE": "y",
    "CONFIG_TMPFS": "y",
    "CONFIG_SHMEM": "y",
    "CONFIG_OVERLAY_FS": "y",
    "CONFIG_DEVTMPFS": "y",
    "CONFIG_DEVTMPFS_MOUNT": "y",
    "CONFIG_PROC_FS": "y",
    "CONFIG_SYSFS": "y",
    "CONFIG_UNIX98_PTYS": "y",
    "CONFIG_TTY": "y",
    "CONFIG_VT": "y",
    "CONFIG_VT_CONSOLE": "y",
    "CONFIG_BINFMT_ELF": "y",
    "CONFIG_BINFMT_SCRIPT": "y",
    # Networking needed before userspace modules can be loaded.
    "CONFIG_NET": "y",
    "CONFIG_IPV6": "y",
    "CONFIG_CPU_FREQ": "y",
    "CONFIG_CPUFREQ_DT": "y",
    "CONFIG_ARM_ARMADA_37XX_CPUFREQ": "y",
    "CONFIG_PACKET": "y",
    "CONFIG_UNIX": "y",
    "CONFIG_INET": "y",
    "CONFIG_NETDEVICES": "y",
    "CONFIG_NET_CORE": "y",
    "CONFIG_BRIDGE": "y",
    "CONFIG_NET_DSA": "y",
    "CONFIG_NET_DSA_TAG_QCA": "y",
    "CONFIG_NET_DSA_AR8236": "y",
    "CONFIG_NET_SWITCHDEV": "y",
    "CONFIG_NET_DEVLINK": "y",
    "CONFIG_PHYLINK": "y",
    "CONFIG_PHYLIB": "y",
    "CONFIG_MDIO_BUS": "y",
    "CONFIG_FIXED_PHY": "y",
    "CONFIG_MVNETA": "y",
    # OC200 Armada 3720 USB 2.0 host path and common removable storage.
    "CONFIG_USB": "y",
    "CONFIG_USB_COMMON": "y",
    "CONFIG_USB_XHCI_HCD": "y",
    "CONFIG_USB_XHCI_PLATFORM": "y",
    "CONFIG_USB_XHCI_MVEBU": "y",
    "CONFIG_USB_EHCI_HCD": "y",
    "CONFIG_USB_EHCI_HCD_ORION": "y",
    "CONFIG_USB_EHCI_HCD_PLATFORM": "y",
    "CONFIG_LEDS_GPIO": "y",
    "CONFIG_USB_DWC3": "y",
    "CONFIG_USB_DWC3_HOST": "y",
    "CONFIG_USB_DWC3_GENERIC_PLAT": "y",
    "CONFIG_USB_PHY": "y",
    "CONFIG_USB_ULPI_BUS": "y",
    "CONFIG_PHY_MVEBU_A3700_UTMI": "y",
    "CONFIG_PHY_MVEBU_A3700_COMPHY": "y",
    "CONFIG_USB_STORAGE": "y",
    "CONFIG_USB_UAS": "y",
    "CONFIG_SCSI": "y",
    "CONFIG_BLK_DEV_SD": "y",
    # OC200 storage and boot hardware.
    "CONFIG_MMC": "y",
    "CONFIG_MMC_BLOCK": "y",
    "CONFIG_MMC_BLOCK_MINORS": "32",
    "CONFIG_MMC_SDHCI": "y",
    "CONFIG_MMC_SDHCI_PLTFM": "y",
    "CONFIG_MMC_SDHCI_XENON": "y",
    "CONFIG_MTD": "y",
    "CONFIG_MTD_CMDLINE_PARTS": "y",
    "CONFIG_MTD_OF_PARTS": "y",
    "CONFIG_MTD_SPI_NOR": "y",
    "CONFIG_SPI": "y",
    "CONFIG_SPI_MASTER": "y",
    "CONFIG_SPI_MEM": "y",
    "CONFIG_SPI_ARMADA_3700": "y",
    "CONFIG_SERIAL_MVEBU_UART": "y",
    "CONFIG_SERIAL_MVEBU_CONSOLE": "y",
    "CONFIG_PRINTK": "y",
    # The board uses an MBR/DOS partition map.
    "CONFIG_PARTITION_ADVANCED": "y",
    "CONFIG_MSDOS_PARTITION": "y",
}


def update_config(stock: Path, output: Path) -> None:
    lines = stock.read_text().splitlines()
    positions: dict[str, int] = {}
    for index, line in enumerate(lines):
        if line.startswith("CONFIG_") and "=" in line:
            positions[line.split("=", 1)[0]] = index
        elif line.startswith("# CONFIG_") and line.endswith(" is not set"):
            key = line[2 : -len(" is not set")]
            positions[key] = index

    for key, value in FORCE_BUILTIN.items():
        replacement = f"{key}={value}"
        if key in positions:
            lines[positions[key]] = replacement
        else:
            lines.append(replacement)

    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text("\n".join(lines) + "\n")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("stock", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    update_config(args.stock, args.output)


if __name__ == "__main__":
    main()
