#!/usr/bin/env python3
"""Verify actual Linux release archives before uploading them as release assets."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import tempfile


def field(package, name):
    return subprocess.check_output(["dpkg-deb", "-f", str(package), name], text=True).strip()


def verify(directory, version, device):
    name, arch, runtime, binary, machine = (
        ("trailmate-cardputer-zero", "arm64", "linux-arm64", "usr/lib/trailmate-cardputer-zero/trailmate-cardputer-zero-fbdev", 183)
        if device == "cardputer-zero" else
        ("trailmate-cm0", "armhf", "linux-arm", "usr/bin/trailmate-linux", 40))
    packages = [p for p in directory.rglob("*.deb") if field(p, "Package") == name]
    if len(packages) != 1:
        raise RuntimeError(f"Expected one {name} archive, found {len(packages)}")
    package = packages[0]
    assert field(package, "Architecture") == arch, "Incorrect target architecture"
    assert field(package, "Version") == version, "Package version differs from release"
    depends = field(package, "Depends")
    assert "osmium-tool" in depends and "libicu" in depends, "Missing online-map dependencies"
    with tempfile.TemporaryDirectory(prefix="trailmate-release-verify-") as temporary:
        root = Path(temporary)
        subprocess.run(["dpkg-deb", "-x", str(package), str(root)], check=True)
        executable = root / binary
        header = executable.read_bytes()[:20]
        assert header[:4] == b"\x7fELF" and struct.unpack_from("<H", header, 18)[0] == machine, "Incorrect ELF architecture"
        assert executable.stat().st_mode & 0o111, "Application is not executable"
        producer = root / "usr/lib/trailmate/tmap-producer"
        manifest = json.loads((producer / "producer-manifest.json").read_text(encoding="utf-8-sig"))
        assert manifest["runtime"] == runtime
        assert manifest["sourceRevision"] == "efa12462a526aabad7212cbadfa537ace0b7dbd6"
        assert (producer / "Center-LICENSE").is_file()
        assert (producer / "TmapTool").stat().st_mode & 0o111
        for entry in manifest["files"]:
            if entry["name"].endswith(".pdb"):
                continue
            assert Path(entry["name"]).name == entry["name"], "Invalid producer manifest path"
            assert hashlib.sha256((producer / entry["name"]).read_bytes()).hexdigest() == entry["sha256"], entry["name"]
    if device == "t-display-cm0":
        assert "trailmate-oneseg-backend" in depends, "Missing native 1seg backend dependency"
        backends = [p for p in directory.rglob("*.deb") if field(p, "Package") == "trailmate-oneseg-backend"]
        assert len(backends) == 1 and field(backends[0], "Architecture") == "armhf"
        provides = field(backends[0], "Provides")
        assert "trailmate-oneseg-backend-cm0-abi-v1" in provides and "trailmate-oneseg-backend-auto-v1" in provides
    print(f"Validated {package.name}: version, ELF, dependencies and pinned producer hashes")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--directory", type=Path, required=True)
    parser.add_argument("--version", required=True)
    parser.add_argument("--device", choices=("cardputer-zero", "t-display-cm0"), required=True)
    args = parser.parse_args()
    verify(args.directory, args.version, args.device)
