#!/usr/bin/env python3
"""Resolve and verify the current upstream Linux and Alpine stable releases."""

import argparse
import hashlib
import json
import os
import re
import shutil
import tempfile
from pathlib import Path
from urllib.request import urlopen

ALPINE = "https://dl-cdn.alpinelinux.org/alpine"
KERNEL = "https://cdn.kernel.org/pub/linux/kernel"


def read(url):
    with urlopen(url, timeout=60) as response:
        return response.read()


def version_key(version):
    return tuple(map(int, version.split(".")))


def resolve():
    import yaml

    linux_releases = json.loads(read("https://www.kernel.org/releases.json"))
    kernel = linux_releases["latest_stable"]["version"]
    if not re.fullmatch(r"\d+\.\d+(?:\.\d+)?", kernel):
        raise ValueError("invalid stable Linux version")
    kernel_dir = f"{KERNEL}/v{kernel.split('.')[0]}.x"
    kernel_file = f"linux-{kernel}.tar.xz"
    sums = read(f"{kernel_dir}/sha256sums.asc").decode()
    match = re.search(rf"^([a-f0-9]{{64}})[ \t]+{re.escape(kernel_file)}$",
                      sums, re.MULTILINE)
    if not match:
        raise ValueError(f"missing kernel.org SHA-256 for {kernel_file}")

    releases = yaml.safe_load(read(f"{ALPINE}/latest-stable/releases/aarch64/latest-releases.yaml"))
    mini = max(
        (r for r in releases if r["flavor"] == "alpine-minirootfs"
         and r["arch"] == "aarch64" and re.fullmatch(r"\d+\.\d+\.\d+", str(r["version"]))),
        key=lambda r: version_key(str(r["version"])),
    )
    alpine = str(mini["version"])
    branch = "v" + alpine.rsplit(".", 1)[0]
    alpine_file = f"alpine-minirootfs-{alpine}-aarch64.tar.gz"
    if mini["branch"] != branch or mini["file"] != alpine_file:
        raise ValueError("unexpected Alpine stable release metadata")
    alpine_sha = mini["sha256"]
    if not re.fullmatch(r"[a-f0-9]{64}", alpine_sha):
        raise ValueError("invalid Alpine release checksum")

    return {
        "linux": {"version": kernel, "file": kernel_file,
                  "url": f"{kernel_dir}/{kernel_file}", "sha256": match[1]},
        "alpine": {"version": alpine, "branch": branch, "file": alpine_file,
                   "url": f"{ALPINE}/{branch}/releases/aarch64/{alpine_file}",
                   "sha256": alpine_sha},
    }


def digest(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def fetch(item, cache):
    path = cache / item["file"]
    if path.is_file() and digest(path) == item["sha256"]:
        print(f"Verified cached {path.name}")
        return
    with tempfile.NamedTemporaryFile(dir=cache, delete=False) as tmp:
        temp_path = Path(tmp.name)
        try:
            with urlopen(item["url"], timeout=60) as response:
                shutil.copyfileobj(response, tmp, length=1024 * 1024)
            tmp.close()
            if digest(temp_path) != item["sha256"]:
                raise ValueError(f"checksum mismatch for {path.name}")
            temp_path.replace(path)
        finally:
            temp_path.unlink(missing_ok=True)
    print(f"Verified {path.name}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("manifest", type=Path)
    parser.add_argument("--fetch", type=Path, metavar="DOWNLOAD_DIR")
    args = parser.parse_args()
    if args.fetch is None:
        versions = resolve()
        args.manifest.write_text(json.dumps(versions, indent=2) + "\n")
        print(json.dumps(versions, indent=2))
        if output := os.getenv("GITHUB_OUTPUT"):
            with open(output, "a") as stream:
                for name, item in versions.items():
                    print(f"{name}={item['version']}", f"{name}_sha={item['sha256']}",
                          file=stream, sep="\n")
    else:
        args.fetch.mkdir(parents=True, exist_ok=True)
        for item in json.loads(args.manifest.read_text()).values():
            fetch(item, args.fetch)


if __name__ == "__main__":
    main()
