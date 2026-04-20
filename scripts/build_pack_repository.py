from __future__ import annotations

import argparse
import hashlib
import json
import shutil
import subprocess
import sys
import tempfile
from datetime import datetime, timezone
from pathlib import Path
from zipfile import ZIP_DEFLATED, ZipFile, ZipInfo


PACKAGE_SCHEMA_VERSION = 1
ARCHIVE_LAYOUT = "trailmate-pack-v1"
FIXED_ZIP_DT = (2026, 1, 1, 0, 0, 0)
TOP_LEVEL_PACKAGE_FILES = ("package.ini", "DESCRIPTION.txt", "README.md")
PAYLOAD_GROUPS = ("fonts", "locales", "ime")
PACKAGE_EXCLUDE_NAMES = {".gitignore", "build.ini", "charset.txt"}


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Build installable locale-pack zip archives and a package catalog for GitHub Pages."
    )
    parser.add_argument("--pack-root", default="packs", help="Repository pack source root")
    parser.add_argument("--site-root", default="site", help="Pages site root")
    parser.add_argument("--node-exe", default="node", help="Node.js executable for font generation")
    parser.add_argument(
        "--npm-exe",
        default=None,
        help="Optional npm executable override for font generation",
    )
    parser.add_argument(
        "--catalog-path",
        default=None,
        help="Optional explicit output path for the generated catalog JSON",
    )
    return parser.parse_args()


def clear_directory_contents(path: Path) -> None:
    path.mkdir(parents=True, exist_ok=True)
    for child in path.iterdir():
        if child.is_dir():
            shutil.rmtree(child)
        else:
            child.unlink()


def parse_key_value_file(path: Path) -> dict[str, str]:
    data: dict[str, str] = {}
    for raw_line in path.read_text(encoding="utf-8").splitlines():
        line = raw_line.strip()
        if not line or line.startswith("#") or line.startswith(";"):
            continue
        if "=" not in line:
            continue
        key, value = line.split("=", 1)
        key = key.strip().lower()
        value = value.strip()
        if key:
            data[key] = value
    return data


def split_csv(value: str | None) -> list[str]:
    if not value:
        return []
    return [item.strip() for item in value.split(",") if item.strip()]


def parse_int(value: str | None, default: int = 0) -> int:
    if value is None or value == "":
        return default
    return int(value, 10)


def parse_bool(value: str | None, default: bool = False) -> bool:
    if value is None:
        return default
    normalized = value.strip().lower()
    if normalized in {"1", "true", "yes", "on"}:
        return True
    if normalized in {"0", "false", "no", "off"}:
        return False
    return default


def build_font_if_missing(
    stage_font_dir: Path,
    repo_root: Path,
    node_exe: str,
    npm_exe: str | None,
) -> None:
    build_path = stage_font_dir / "build.ini"
    font_bin_path = stage_font_dir / "font.bin"
    if font_bin_path.exists():
        return
    if not build_path.exists():
        raise FileNotFoundError(f"Missing font.bin and build.ini for {stage_font_dir}")

    build = parse_key_value_file(build_path)
    font_source = build.get("font")
    if not font_source:
        raise ValueError(f"build.ini missing font=... in {build_path}")

    charset_file = stage_font_dir / "charset.txt"
    if not charset_file.exists():
        raise FileNotFoundError(f"Missing charset.txt for generated font: {charset_file}")

    generator = repo_root / "tools" / "generate_binfont_with_lv_font_conv.py"
    if not generator.exists():
        raise FileNotFoundError(f"Missing generator script: {generator}")

    command = [
        sys.executable,
        str(generator),
        "--font",
        str((repo_root / font_source).resolve()),
        "--charset-file",
        str(charset_file.resolve()),
        "--output",
        str(font_bin_path.resolve()),
        "--size",
        str(parse_int(build.get("size"), 16)),
        "--bpp",
        str(parse_int(build.get("bpp"), 2)),
        "--node-exe",
        node_exe,
    ]
    if npm_exe:
        command.extend(["--npm-exe", npm_exe])
    if parse_bool(build.get("no_compress"), default=False):
        command.append("--no-compress")
    if parse_bool(build.get("no_prefilter"), default=False):
        command.append("--no-prefilter")
    if parse_bool(build.get("no_kerning"), default=False):
        command.append("--no-kerning")

    subprocess.run(command, check=True)


def stage_bundle(
    source_bundle_dir: Path,
    repo_root: Path,
    node_exe: str,
    npm_exe: str | None,
) -> Path:
    temp_dir = Path(tempfile.mkdtemp(prefix=f"trailmate-pack-{source_bundle_dir.name}-"))
    stage_bundle_dir = temp_dir / source_bundle_dir.name
    shutil.copytree(
        source_bundle_dir,
        stage_bundle_dir,
        ignore=shutil.ignore_patterns("__pycache__", "*.pyc"),
    )

    fonts_root = stage_bundle_dir / "fonts"
    if fonts_root.is_dir():
        for font_dir in sorted(path for path in fonts_root.iterdir() if path.is_dir()):
            build_font_if_missing(font_dir, repo_root, node_exe, npm_exe)

    return stage_bundle_dir


def iter_archive_sources(stage_bundle_dir: Path) -> list[tuple[Path, str]]:
    files: list[tuple[Path, str]] = []

    for name in TOP_LEVEL_PACKAGE_FILES:
        source = stage_bundle_dir / name
        if source.is_file():
            files.append((source, name))

    for group in PAYLOAD_GROUPS:
        group_dir = stage_bundle_dir / group
        if not group_dir.is_dir():
            continue
        for source in sorted(path for path in group_dir.rglob("*") if path.is_file()):
            if source.name in PACKAGE_EXCLUDE_NAMES:
                continue
            relative = source.relative_to(stage_bundle_dir).as_posix()
            files.append((source, f"payload/{relative}"))

    return files


def write_deterministic_zip(files: list[tuple[Path, str]], destination: Path) -> None:
    destination.parent.mkdir(parents=True, exist_ok=True)
    with ZipFile(destination, "w") as archive:
        for source, archive_name in sorted(files, key=lambda item: item[1]):
            data = source.read_bytes()
            info = ZipInfo(filename=archive_name, date_time=FIXED_ZIP_DT)
            info.compress_type = ZIP_DEFLATED
            info.external_attr = 0o100644 << 16
            archive.writestr(info, data)


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def read_package_text(stage_bundle_dir: Path, relative_path: str | None) -> str:
    if not relative_path:
        return ""
    path = stage_bundle_dir / relative_path
    if not path.is_file():
        raise FileNotFoundError(f"Missing package text file: {path}")
    return path.read_text(encoding="utf-8").strip()


def collect_font_records(stage_bundle_dir: Path) -> list[dict[str, object]]:
    records: list[dict[str, object]] = []
    fonts_root = stage_bundle_dir / "fonts"
    if not fonts_root.is_dir():
        return records

    for manifest_path in sorted(fonts_root.glob("*/manifest.ini")):
        manifest = parse_key_value_file(manifest_path)
        records.append(
            {
                "id": manifest.get("id", manifest_path.parent.name),
                "display_name": manifest.get("display_name", manifest_path.parent.name),
                "usage": manifest.get("usage", "both"),
                "estimated_ram_bytes": parse_int(manifest.get("estimated_ram_bytes"), 0),
            }
        )
    return records


def collect_locale_records(stage_bundle_dir: Path) -> list[dict[str, object]]:
    records: list[dict[str, object]] = []
    locales_root = stage_bundle_dir / "locales"
    if not locales_root.is_dir():
        return records

    for manifest_path in sorted(locales_root.glob("*/manifest.ini")):
        manifest = parse_key_value_file(manifest_path)
        records.append(
            {
                "id": manifest.get("id", manifest_path.parent.name),
                "display_name": manifest.get("display_name", manifest_path.parent.name),
                "native_name": manifest.get("native_name", manifest.get("display_name", manifest_path.parent.name)),
                "ui_font_pack_id": manifest.get("ui_font_pack", ""),
                "content_font_pack_id": manifest.get(
                    "content_font_pack", manifest.get("ui_font_pack", "")
                ),
                "ime_pack_id": manifest.get("ime_pack", ""),
            }
        )
    return records


def collect_ime_records(stage_bundle_dir: Path) -> list[dict[str, object]]:
    records: list[dict[str, object]] = []
    ime_root = stage_bundle_dir / "ime"
    if not ime_root.is_dir():
        return records

    for manifest_path in sorted(ime_root.glob("*/manifest.ini")):
        manifest = parse_key_value_file(manifest_path)
        records.append(
            {
                "id": manifest.get("id", manifest_path.parent.name),
                "display_name": manifest.get("display_name", manifest_path.parent.name),
                "backend": manifest.get("backend", "none"),
            }
        )
    return records


def build_package_entry(
    stage_bundle_dir: Path,
    package_manifest: dict[str, str],
    archive_relative_path: str,
    archive_path: Path,
) -> dict[str, object]:
    fonts = collect_font_records(stage_bundle_dir)
    locales = collect_locale_records(stage_bundle_dir)
    ime = collect_ime_records(stage_bundle_dir)

    description_text = read_package_text(stage_bundle_dir, package_manifest.get("description"))
    readme_path = package_manifest.get("readme")
    readme_relative_path = readme_path if readme_path and (stage_bundle_dir / readme_path).is_file() else ""

    return {
        "id": package_manifest["id"],
        "package_type": package_manifest.get("package_type", "locale-bundle"),
        "version": package_manifest["version"],
        "display_name": package_manifest.get("display_name", package_manifest["id"]),
        "summary": package_manifest.get("summary", ""),
        "description": description_text,
        "author": package_manifest.get("author", "Trail Mate"),
        "homepage": package_manifest.get("homepage", ""),
        "min_firmware_version": package_manifest.get("min_firmware_version", ""),
        "supported_memory_profiles": split_csv(package_manifest.get("supported_memory_profiles")),
        "tags": split_csv(package_manifest.get("tags")),
        "provides": {
            "locales": locales,
            "fonts": fonts,
            "ime": ime,
        },
        "runtime": {
            "estimated_unique_font_ram_bytes": sum(
                {record["id"]: int(record["estimated_ram_bytes"]) for record in fonts}.values()
            ),
            "locale_count": len(locales),
            "font_count": len(fonts),
            "ime_count": len(ime),
        },
        "archive": {
            "layout": ARCHIVE_LAYOUT,
            "path": archive_relative_path.replace("\\", "/"),
            "size_bytes": archive_path.stat().st_size,
            "sha256": sha256_file(archive_path),
        },
        "files": {
            "package_manifest": "package.ini",
            "description": package_manifest.get("description", ""),
            "readme": readme_relative_path,
        },
    }


def discover_source_bundles(pack_root: Path) -> list[Path]:
    bundles: list[Path] = []
    for candidate in sorted(path for path in pack_root.iterdir() if path.is_dir()):
        if (candidate / "package.ini").is_file():
            bundles.append(candidate)
    return bundles


def build_pack_repository(
    pack_root: Path,
    site_root: Path,
    node_exe: str = "node",
    npm_exe: str | None = None,
    catalog_path: Path | None = None,
) -> dict[str, object]:
    pack_root = pack_root.resolve()
    site_root = site_root.resolve()
    repo_root = pack_root.parent.resolve()
    assets_dir = site_root / "assets" / "packs"
    clear_directory_contents(assets_dir)

    entries: list[dict[str, object]] = []
    temp_roots: list[Path] = []
    try:
        for bundle_dir in discover_source_bundles(pack_root):
            package_manifest = parse_key_value_file(bundle_dir / "package.ini")
            if package_manifest.get("kind") != "package":
                continue
            if "id" not in package_manifest or "version" not in package_manifest:
                raise ValueError(f"package.ini missing id/version in {bundle_dir}")

            stage_bundle_dir = stage_bundle(bundle_dir, repo_root, node_exe, npm_exe)
            temp_roots.append(stage_bundle_dir.parent)

            archive_name = f"{package_manifest['id']}-{package_manifest['version']}.zip"
            archive_path = assets_dir / archive_name
            write_deterministic_zip(iter_archive_sources(stage_bundle_dir), archive_path)

            archive_relative_path = archive_path.relative_to(site_root).as_posix()
            entries.append(
                build_package_entry(
                    stage_bundle_dir,
                    package_manifest,
                    archive_relative_path,
                    archive_path,
                )
            )
    finally:
        for temp_root in temp_roots:
            shutil.rmtree(temp_root, ignore_errors=True)

    entries.sort(key=lambda entry: str(entry["display_name"]).lower())
    payload = {
        "schema_version": PACKAGE_SCHEMA_VERSION,
        "archive_layout": ARCHIVE_LAYOUT,
        "generated_at": datetime.now(timezone.utc).isoformat().replace("+00:00", "Z"),
        "packages": entries,
    }

    output_path = catalog_path or (site_root / "data" / "packs.json")
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(json.dumps(payload, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    return payload


def main() -> int:
    args = parse_args()
    build_pack_repository(
        pack_root=Path(args.pack_root),
        site_root=Path(args.site_root),
        node_exe=args.node_exe,
        npm_exe=args.npm_exe,
        catalog_path=Path(args.catalog_path) if args.catalog_path else None,
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
