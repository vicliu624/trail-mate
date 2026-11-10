"""Build the optional Linux LVGL font and language package without network access."""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess

from fontTools.ttLib import TTFont

ROOT = Path(__file__).resolve().parents[1]
MIB = 1024 * 1024
FONT_SPECS = (
    ("linux-cjk", "Noto CJK (Chinese/Korean)", "NotoSansCJKsc-Regular.otf", 16, 32 * MIB),
    ("linux-cjk-jp", "Noto CJK (Japanese)", "NotoSansCJKjp-Regular.otf", 16, 32 * MIB),
    ("linux-latin", "Noto Latin and Cyrillic", "NotoSans-Regular.ttf", 16, 2 * MIB),
    ("linux-color-emoji", "TrailMate Color Emoji", "linux/TrailMateColorEmoji14.ttf", 14, 4 * MIB),
)


def manifest(path: Path) -> dict[str, str]:
    return {key.strip(): value.strip() for line in path.read_text(encoding="utf-8").splitlines()
            if line and not line.startswith(("#", ";")) and "=" in line
            for key, value in [line.split("=", 1)]}


def write_manifest(path: Path, values: dict[str, str]) -> None:
    path.write_text("".join(f"{key}={value}\n" for key, value in values.items()), encoding="utf-8")


def ranges(codepoints) -> str:
    result = []
    first = previous = None
    for codepoint in sorted(codepoints):
        if first is None:
            first = previous = codepoint
        elif codepoint == previous + 1:
            previous = codepoint
        else:
            result.append(f"0x{first:X}" if first == previous else f"0x{first:X}-0x{previous:X}")
            first = previous = codepoint
    if first is not None:
        result.append(f"0x{first:X}" if first == previous else f"0x{first:X}-0x{previous:X}")
    return ",".join(result) + "\n"


def build(stage: Path, version: str) -> dict:
    if stage.exists() and any(stage.iterdir()):
        raise ValueError("Use an empty staging directory; existing files will not be removed")
    system = stage / "usr/share/trailmate/packs"
    receipt = {"schema": "trailmate.linux-font-pack.v1", "version": version,
               "minimum_app_version": "1.2.0", "fonts": [], "locales": []}
    for identifier, label, relative, pixel_size, budget in FONT_SPECS:
        source = ROOT / "tools/fonts" / relative
        font = TTFont(source)
        cmap = {point: name for point, name in font.getBestCmap().items() if name != ".notdef"}
        directory = system / "fonts" / identifier
        directory.mkdir(parents=True)
        filename = "font" + source.suffix
        shutil.copyfile(source, directory / filename)
        values = {"kind": "font", "id": identifier, "targets": "linux", "display_name": label,
                  "source": "freetype", "usage": "both",
                  "file": filename, "pixel_size": str(pixel_size), "estimated_ram_bytes": str(budget),
                  "ranges": "ranges.txt"}
        if identifier == "linux-color-emoji":
            values["color_emoji"] = "true"
            origin = json.loads((source.parent / "font-source.json").read_text(encoding="utf-8"))
            if hashlib.sha256(source.read_bytes()).hexdigest() != origin["output_sha256"]:
                raise ValueError("Derived color emoji font does not match its provenance")
            if len(font["CBLC"].strikes) != 1 or font["CBLC"].strikes[0].bitmapSizeTable.ppemY != 14:
                raise ValueError("The color emoji font must have a compact 14 ppem strike")
            shutil.copyfile(source.parent / "font-source.json", directory / "font-source.json")
            license_file = source.parent / "LICENSE-NotoColorEmoji.txt"
        elif source.suffix == ".otf":
            license_file = ROOT / "packs/ja-broadcast/fonts/ja-broadcast-cjk/LICENSE-Noto.txt"
        else:
            license_file = ROOT / "tools/fonts/NotoSans-LICENSE.txt"
        shutil.copyfile(license_file, directory / "LICENSE.txt")
        write_manifest(directory / "manifest.ini", values)
        (directory / "ranges.txt").write_text(ranges(cmap), encoding="utf-8")
        receipt["fonts"].append({"id": identifier, "source": "tools/fonts/" + relative,
                                 "sha256": hashlib.sha256(source.read_bytes()).hexdigest(),
                                 "codepoints": len(cmap), "pixel_size": pixel_size,
                                 "estimated_ram_bytes": budget})
        font.close()
    for path in sorted((ROOT / "packs").glob("*/locales/*/manifest.ini")):
        values = manifest(path)
        if values.get("translation_status") != "release":
            continue
        identifier = values["id"]
        directory = system / "locales" / identifier
        directory.mkdir(parents=True)
        font_id = "linux-cjk-jp" if identifier == "ja" else (
            "linux-cjk" if identifier in ("zh-Hans", "zh-Hant", "ko") else "linux-latin")
        values = {key: value for key, value in values.items()
                  if not key.startswith("tdeck_pro_") and "supplement" not in key}
        # Native Linux input methods are owned by the system/toolkit frontend.
        # The shared ESP Pinyin engine is not a display-language dependency here.
        values.pop("ime_pack", None)
        values.update(ui_font_pack=font_id, content_font_pack=font_id,
                      preferred_content_supplement_packs="linux-color-emoji")
        write_manifest(directory / "manifest.ini", values)
        shutil.copyfile(path.parent / values["strings"], directory / values["strings"])
        receipt["locales"].append(identifier)
    (system / "linux-font-pack.json").write_text(json.dumps(receipt, indent=2) + "\n", encoding="utf-8")
    control = stage / "DEBIAN"
    control.mkdir(parents=True)
    (control / "control").write_text(
        f"Package: trailmate-linux-fonts\nVersion: {version}\nArchitecture: all\nSection: fonts\nPriority: optional\n"
        "Maintainer: TrailMate Maintainers <vicliu624@users.noreply.github.com>\n"
        "Depends: trailmate (>= 1.2.0) | trailmate-cardputer-zero (>= 1.2.0) | trailmate-cm0 (>= 1.2.0)\n"
        "Description: Optional full CJK, color emoji and display languages for TrailMate Linux LVGL\n"
        " Disk-backed Noto fonts, compact CBDT emoji and released language tables.\n"
        " Glyph images use a bounded runtime cache. User data and identity stay unchanged.\n", encoding="utf-8")
    stage.chmod(0o755)
    for path in stage.rglob("*"):
        path.chmod(0o755 if path.is_dir() else 0o644)
    return receipt


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--stage", type=Path, required=True)
    parser.add_argument("--version", required=True)
    parser.add_argument("--deb", type=Path)
    args = parser.parse_args()
    if not re.fullmatch(r"\d+\.\d+\.\d+(?:~[A-Za-z0-9.+-]+)?", args.version):
        parser.error("Use the project's Debian semantic version")
    receipt = build(args.stage, args.version)
    if args.deb:
        args.deb.parent.mkdir(parents=True, exist_ok=True)
        subprocess.run(["dpkg-deb", "--build", "--root-owner-group", "-Zxz", str(args.stage), str(args.deb)], check=True)
    print(json.dumps(receipt, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
