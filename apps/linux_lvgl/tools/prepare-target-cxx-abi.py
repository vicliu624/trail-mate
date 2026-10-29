#!/usr/bin/env python3
"""Prepare an explicit target-distribution C++ ABI overlay (Python >= 3.12)."""
import argparse
import hashlib
import json
from pathlib import Path
import tarfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("archive", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    output = args.output.resolve()
    if output.exists():
        parser.error("output must be a new directory; do not overlay an existing sysroot")
    with tarfile.open(args.archive) as archive:
        for item in archive.getmembers():
            name = Path(item.name)
            if (name.is_absolute() or ".." in name.parts or
                not (item.isdir() or item.isfile()) or
                not (item.name.startswith("usr/include/c++/") or
                     item.name.startswith("usr/include/arm-linux-gnueabihf/c++/") or
                     item.name.startswith("usr/lib/arm-linux-gnueabihf/libstdc++.so."))):
                parser.error(f"unexpected archive entry: {item.name}")
        output.mkdir(parents=True)
        archive.extractall(output, filter="data")
    versions = list((output / "usr/include/c++").iterdir())
    libraries = list((output / "usr/lib/arm-linux-gnueabihf").glob("libstdc++.so.*"))
    if len(versions) != 1 or len(libraries) != 1:
        parser.error("archive must contain exactly one header version and one runtime")
    config = output / "usr/include/arm-linux-gnueabihf/c++" / versions[0].name / "bits/c++config.h"
    if not config.is_file():
        parser.error("archive is missing the target c++config.h")
    (libraries[0].parent / "libstdc++.so").symlink_to(libraries[0].name)
    provenance = {
        "archive_sha256": hashlib.file_digest(args.archive.open("rb"), "sha256").hexdigest(),
        "header_major": versions[0].name,
        "runtime": libraries[0].name,
        "atomic_lock_policy": "#define _GLIBCXX_HAVE_ATOMIC_LOCK_POLICY 1" in config.read_text(),
    }
    (output / "target-cxx-abi.json").write_text(json.dumps(provenance, indent=2) + "\n")
    print(json.dumps(provenance, indent=2))


if __name__ == "__main__":
    main()
