"""Verify the separate Linux font DEB, including ownership and immutable payloads."""
import argparse
import hashlib
from io import BytesIO
import json
from pathlib import Path, PurePosixPath
import subprocess
import tarfile


def verify(package: Path, version: str) -> dict:
    for field, expected in (("Package", "trailmate-linux-fonts"), ("Architecture", "all"), ("Version", version)):
        actual = subprocess.check_output(["dpkg-deb", "-f", str(package), field], text=True).strip()
        if actual != expected:
            raise ValueError(f"Incorrect {field}: {actual}")
    data = subprocess.check_output(["dpkg-deb", "--fsys-tarfile", str(package)])
    prefix = "usr/share/trailmate/packs/"
    files = {}
    with tarfile.open(fileobj=BytesIO(data), mode="r:") as archive:
        for member in archive.getmembers():
            name = str(PurePosixPath(member.name))
            if name.startswith("/") or ".." in PurePosixPath(name).parts:
                raise ValueError("Unsafe package path")
            if member.uid != 0 or member.gid != 0:
                raise ValueError(f"Payload is not owned by root: {name}")
            if member.isdir():
                ancestors = {".", "usr", "usr/share", "usr/share/trailmate", "usr/share/trailmate/packs"}
                if name not in ancestors and not name.startswith(prefix):
                    raise ValueError(f"Unexpected directory outside the font package: {name}")
                if member.mode & 0o777 != 0o755:
                    raise ValueError(f"Unexpected directory permissions: {name}")
                continue
            if not member.isfile() or not name.startswith(prefix) or member.mode & 0o777 != 0o644:
                raise ValueError(f"Unexpected payload path/type/permissions: {name}")
            files[name[len(prefix):]] = archive.extractfile(member).read()
    receipt = json.loads(files["linux-font-pack.json"])
    if receipt["schema"] != "trailmate.linux-font-pack.v1" or receipt["version"] != version:
        raise ValueError("Font package receipt does not match its control version")
    if receipt["minimum_app_version"] != "1.2.0" or len(receipt["fonts"]) != 4:
        raise ValueError("Missing font package compatibility or source data")
    depends = subprocess.check_output(["dpkg-deb", "-f", str(package), "Depends"], text=True)
    if "trailmate (>= 1.2.0)" not in depends:
        raise ValueError("The font package must require a compatible application")
    for font in receipt["fonts"]:
        directory = "fonts/" + font["id"] + "/"
        values = dict(line.split("=", 1) for line in files[directory + "manifest.ini"].decode().splitlines())
        if values["source"] != "freetype" or values["targets"] != "linux":
            raise ValueError("A native Linux font must not masquerade as an ESP binfont")
        filename = values["file"]
        if PurePosixPath(filename).name != filename:
            raise ValueError("Font payload must stay in its own directory")
        payload = files[directory + filename]
        if hashlib.sha256(payload).hexdigest() != font["sha256"]:
            raise ValueError("Font payload does not match its source SHA256")
        if payload[:4] not in (b"OTTO", b"\x00\x01\x00\x00"):
            raise ValueError("Missing OpenType font signature")
        if b"SIL OPEN FONT LICENSE" not in files[directory + "LICENSE.txt"]:
            raise ValueError("Missing font redistribution license")
        if not files[directory + "ranges.txt"].strip():
            raise ValueError("Missing actual font coverage")
    font_ids = {font["id"] for font in receipt["fonts"]}
    if "zh-Hans" not in receipt["locales"]:
        raise ValueError("Missing released Simplified Chinese language")
    for locale in receipt["locales"]:
        directory = "locales/" + locale + "/"
        values = dict(line.split("=", 1) for line in files[directory + "manifest.ini"].decode().splitlines())
        if values["translation_status"] != "release" or values["ui_font_pack"] not in font_ids:
            raise ValueError("Unreleased or unresolved display language")
        if "ime_pack" in values:
            raise ValueError("System-owned Linux input methods are not font package dependencies")
        if not files[directory + values["strings"]].strip():
            raise ValueError("Missing translation table")
    return {"package": package.name, "version": version, "fonts": len(font_ids),
            "languages": ["en", *receipt["locales"]], "files": len(files),
            "sha256": hashlib.sha256(package.read_bytes()).hexdigest()}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    source = parser.add_mutually_exclusive_group(required=True)
    source.add_argument("--deb", type=Path)
    source.add_argument("--directory", type=Path)
    parser.add_argument("--version", required=True)
    args = parser.parse_args()
    candidates = [args.deb] if args.deb else list(args.directory.rglob("trailmate-linux-fonts_*.deb"))
    if len(candidates) != 1:
        parser.error("Expected exactly one Linux font package")
    print(json.dumps(verify(candidates[0], args.version), indent=2))


if __name__ == "__main__":
    main()
