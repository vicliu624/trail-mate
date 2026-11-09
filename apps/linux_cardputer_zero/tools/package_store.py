#!/usr/bin/env python3
"""Adapt a verified Cardputer Zero release DEB to the CardputerZero Store policy.

Runtime binaries, resources, producer files and user-data paths are preserved.
Run on Linux with dpkg-deb; the original release asset is never modified.
"""

import argparse
import hashlib
import io
import json
import os
import re
import shutil
import subprocess
import tarfile
import tempfile
from pathlib import Path

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--deb", type=Path, required=True)
parser.add_argument("--sha256", required=True, help="Expected published input checksum")
parser.add_argument("--output", type=Path, required=True)
args = parser.parse_args()
source = args.deb.resolve(strict=True)
output = args.output.resolve()
assert not output.exists(), "Refusing to replace an existing package"
assert hashlib.sha256(source.read_bytes()).hexdigest() == args.sha256.lower(), "Input checksum mismatch"
package = subprocess.check_output(["dpkg-deb", "-f", str(source), "Package"], text=True).strip()
architecture = subprocess.check_output(["dpkg-deb", "-f", str(source), "Architecture"], text=True).strip()
version = subprocess.check_output(["dpkg-deb", "-f", str(source), "Version"], text=True).strip()
assert package == "trailmate-cardputer-zero" and architecture == "arm64"
control_tar = subprocess.check_output(["dpkg-deb", "--ctrl-tarfile", str(source)])
with tarfile.open(fileobj=io.BytesIO(control_tar)) as archive:
    source_epoch = int(max(member.mtime for member in archive.getmembers()))
os.environ["SOURCE_DATE_EPOCH"] = str(source_epoch)
repo = Path(__file__).resolve().parents[3]
output.parent.mkdir(parents=True, exist_ok=True)

with tempfile.TemporaryDirectory(prefix="trailmate-store-") as temporary:
    root = Path(temporary) / "package"
    subprocess.run(["dpkg-deb", "-R", str(source), str(root)], check=True)
    original = {
        str(p.relative_to(root)): hashlib.sha256(p.read_bytes()).hexdigest()
        for p in root.rglob("*") if p.is_file() and "DEBIAN" not in p.relative_to(root).parts
    }
    # Transitive TMAP CMake installs include development-only files. These are
    # unnecessary at runtime and do not belong in an AppStore application.
    removed = []
    for relative in ("usr/include/tmap", "usr/lib/cmake/tmap", "usr/lib/libtmap.a",
                     "usr/lib/udev/rules.d/70-trailmate-cardputer-zero.rules"):
        target = root / relative
        assert target.resolve().is_relative_to(root.resolve())
        removed.extend(k for k in original if k == relative or k.startswith(relative + "/"))
        if target.is_dir():
            shutil.rmtree(target)
        elif target.exists():
            target.unlink()

    # The store requires shared directory names to start with the package name.
    # "trailmate" owns both existing runtime namespaces without relocating the
    # executable, its bundled producer or any user's settings and messages.
    control = root / "DEBIAN/control"
    text = control.read_text()
    text = re.sub(r"(?m)^Package: .*$", "Package: trailmate", text)
    text = re.sub(r"(?m)^Maintainer: .*$", "Maintainer: vicliu624 <vicliu624@users.noreply.github.com>", text)
    text = re.sub(r"(?m)^Installed-Size: .*\n?", "", text)
    assert not re.search(r"(?m)^(Conflicts|Replaces|Provides):", text)
    text = text.rstrip() + "\nConflicts: trailmate-cardputer-zero\nReplaces: trailmate-cardputer-zero\nProvides: trailmate-cardputer-zero (= " + version + ")\nHomepage: https://github.com/vicliu624/trail-mate\n"

    # udev RUN is forbidden by the store. tmpfiles applies this same narrow
    # sysfs permission at boot and package installation, with no root app/unit.
    rules = root / "usr/lib/tmpfiles.d/trailmate.conf"
    rules.parent.mkdir(parents=True, exist_ok=True)
    rules.write_text("# Allow the gpio group to use the Cap EXT5V supply.\nz /sys/class/leds/ext_5v_out/brightness 0664 root gpio - -\n")
    postinst = root / "DEBIAN/postinst"
    postinst.write_text("#!/bin/sh\nset -eu\nif [ \"${1:-}\" = configure ] && command -v systemd-tmpfiles >/dev/null 2>&1; then\n    systemd-tmpfiles --create /usr/lib/tmpfiles.d/trailmate.conf || true\nfi\n")
    postinst.chmod(0o755)
    license_path = root / "usr/share/doc/trailmate/COPYING"
    license_path.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(repo / "LICENSE", license_path)

    retained = 0
    for relative, digest in original.items():
        if relative in removed:
            continue
        assert hashlib.sha256((root / relative).read_bytes()).hexdigest() == digest, relative
        retained += 1
    files = sorted(p for p in root.rglob("*") if p.is_file() and "DEBIAN" not in p.relative_to(root).parts)
    installed_size = (sum(p.stat().st_size for p in files) + 1023) // 1024
    control.write_text(text + f"Installed-Size: {installed_size}\n")
    (root / "DEBIAN/md5sums").write_text("".join(
        hashlib.md5(p.read_bytes()).hexdigest() + "  " + str(p.relative_to(root)) + "\n" for p in files))
    subprocess.run(["dpkg-deb", "--root-owner-group", "-b", str(root), str(output)], check=True)

report = {"input_sha256": args.sha256.lower(), "package": "trailmate", "version": version,
          "architecture": architecture, "output": output.name,
          "sha256": hashlib.sha256(output.read_bytes()).hexdigest(),
          "size": output.stat().st_size, "source_date_epoch": source_epoch,
          "unchanged_runtime_files": retained,
          "removed_development_and_udev_files": len(removed)}
output.with_suffix(".verification.json").write_text(json.dumps(report, indent=2) + "\n")
print(json.dumps(report, indent=2))
