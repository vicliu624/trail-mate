#!/usr/bin/env python3

from __future__ import annotations

import argparse
from pathlib import Path


def resolve_font_pack_dir(pack_root: Path, font_pack_id: str | None) -> Path:
    fonts_root = pack_root / "fonts"
    if font_pack_id:
        font_pack_dir = fonts_root / font_pack_id
        if not font_pack_dir.is_dir():
            raise FileNotFoundError(f"font pack directory not found: {font_pack_dir}")
        return font_pack_dir

    candidates = sorted(path for path in fonts_root.iterdir() if path.is_dir()) if fonts_root.is_dir() else []
    if len(candidates) != 1:
        raise RuntimeError(
            "unable to infer font pack directory; pass --font-pack-id or keep exactly one directory under <pack-root>/fonts"
        )
    return candidates[0]


def iter_manifest_texts(pack_root: Path):
    for manifest in sorted(pack_root.rglob("manifest.ini")):
        for raw_line in manifest.read_text(encoding="utf-8").splitlines():
            line = raw_line.strip()
            if not line or line.startswith("#") or line.startswith(";") or "=" not in line:
                continue
            key, value = line.split("=", 1)
            key = key.strip().lower()
            value = value.strip()
            if key in {"display_name", "native_name"} and value:
                yield value


def iter_locale_texts(pack_root: Path):
    for strings_file in sorted(pack_root.rglob("strings.tsv")):
        for raw_line in strings_file.read_text(encoding="utf-8").splitlines():
            line = raw_line.rstrip("\r")
            if not line or line.startswith("#") or "\t" not in line:
                continue
            _, localized = line.split("\t", 1)
            if localized:
                yield localized


def iter_extra_texts(extra_paths: list[Path]):
    for path in extra_paths:
        if not path.exists():
            raise FileNotFoundError(f"missing extra chars file: {path}")
        yield path.read_text(encoding="utf-8")

def iter_charset_file_chars(paths: list[Path]):
    for path in paths:
        if not path.exists():
            raise FileNotFoundError(f"missing charset file: {path}")
        for ch in path.read_text(encoding="utf-8"):
            if ch.isspace() or ord(ch) < 0x80:
                continue
            yield ch


def iter_ranked_chars(path: Path, limit: int):
    if not path.exists():
        raise FileNotFoundError(f"missing ranked chars file: {path}")

    emitted = 0
    for ch in path.read_text(encoding="utf-8"):
        if ch.isspace() or ord(ch) < 0x80:
            continue
        yield ch
        emitted += 1
        if limit > 0 and emitted >= limit:
            break


def collect_charset(pack_root: Path,
                    include_manifests: bool,
                    include_locales: bool,
                    extra_paths: list[Path],
                    seed_paths: list[Path],
                    exclude_paths: list[Path],
                    ranked_chars_path: Path | None,
                    ranked_char_limit: int) -> list[str]:
    chars: set[str] = set()

    text_sources: list[str] = []
    if include_manifests:
        text_sources.extend(iter_manifest_texts(pack_root))
    if include_locales:
        text_sources.extend(iter_locale_texts(pack_root))
    text_sources.extend(iter_extra_texts(extra_paths))

    for text in text_sources:
        for ch in text:
            if ch.isspace() or ord(ch) < 0x80:
                continue
            chars.add(ch)

    for ch in iter_charset_file_chars(seed_paths):
        chars.add(ch)

    if ranked_chars_path is not None:
        for ch in iter_ranked_chars(ranked_chars_path, ranked_char_limit):
            chars.add(ch)

    for ch in iter_charset_file_chars(exclude_paths):
        chars.discard(ch)

    return sorted(chars, key=ord)


def write_charset(path: Path, chars: list[str]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    wrapped = ["".join(chars[index : index + 64]) for index in range(0, len(chars), 64)]
    path.write_text("\n".join(wrapped) + ("\n" if wrapped else ""), encoding="utf-8")


def write_ranges(path: Path, chars: list[str]) -> None:
    ranges: list[str] = []
    if chars:
        codes = [ord(ch) for ch in chars]
        start = prev = codes[0]
        for code in codes[1:]:
            if code == prev + 1:
                prev = code
                continue
            ranges.append(f"0x{start:X}" if start == prev else f"0x{start:X}-0x{prev:X}")
            start = prev = code
        ranges.append(f"0x{start:X}" if start == prev else f"0x{start:X}-0x{prev:X}")
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(",".join(ranges) + ("\n" if ranges else ""), encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description="Build charset files for a locale/font pack bundle.")
    parser.add_argument(
        "--pack-root",
        type=Path,
        required=True,
        help="Pack bundle root, for example packs/locale-bundles/zh-Hans",
    )
    parser.add_argument(
        "--font-pack-id",
        default=None,
        help="Font pack directory name under <pack-root>/fonts. If omitted, the tool infers the only font directory.",
    )
    parser.add_argument(
        "--extra-chars-file",
        action="append",
        default=[],
        help="UTF-8 text file with additional glyphs to include. May be passed multiple times.",
    )
    parser.add_argument(
        "--seed-charset-file",
        action="append",
        default=[],
        help="Existing charset file whose glyphs should be unioned into the output. May be passed multiple times.",
    )
    parser.add_argument(
        "--exclude-charset-file",
        action="append",
        default=[],
        help="Existing charset file whose glyphs should be removed from the output. May be passed multiple times.",
    )
    parser.add_argument(
        "--ranked-chars-file",
        type=Path,
        default=None,
        help="UTF-8 ranked glyph file. The first --ranked-char-limit non-ASCII glyphs are added to the output.",
    )
    parser.add_argument(
        "--ranked-char-limit",
        type=int,
        default=0,
        help="Maximum number of glyphs to consume from --ranked-chars-file. Use 0 for all.",
    )
    parser.add_argument(
        "--no-include-manifests",
        action="store_true",
        help="Do not include display_name/native_name text from manifest.ini files under the bundle.",
    )
    parser.add_argument(
        "--no-include-locales",
        action="store_true",
        help="Do not include localized strings.tsv text from the bundle.",
    )
    parser.add_argument(
        "--charset-out",
        type=Path,
        default=None,
        help="Override charset.txt output path. Defaults to <pack-root>/fonts/<font-pack-id>/charset.txt",
    )
    parser.add_argument(
        "--ranges-out",
        type=Path,
        default=None,
        help="Override ranges.txt output path. Defaults to <pack-root>/fonts/<font-pack-id>/ranges.txt",
    )
    args = parser.parse_args()

    pack_root = args.pack_root.resolve()
    extra_paths = [Path(item).resolve() for item in args.extra_chars_file]
    seed_paths = [Path(item).resolve() for item in args.seed_charset_file]
    exclude_paths = [Path(item).resolve() for item in args.exclude_charset_file]
    ranked_chars_path = args.ranked_chars_file.resolve() if args.ranked_chars_file else None
    font_pack_dir = resolve_font_pack_dir(pack_root, args.font_pack_id)
    charset_out = args.charset_out or (font_pack_dir / "charset.txt")
    ranges_out = args.ranges_out or (font_pack_dir / "ranges.txt")

    chars = collect_charset(pack_root,
                            include_manifests=not args.no_include_manifests,
                            include_locales=not args.no_include_locales,
                            extra_paths=extra_paths,
                            seed_paths=seed_paths,
                            exclude_paths=exclude_paths,
                            ranked_chars_path=ranked_chars_path,
                            ranked_char_limit=max(0, args.ranked_char_limit))
    write_charset(charset_out, chars)
    write_ranges(ranges_out, chars)

    print(f"pack_root={pack_root}")
    print(f"font_pack_dir={font_pack_dir}")
    print(f"glyph_count={len(chars)}")
    print(f"include_manifests={0 if args.no_include_manifests else 1}")
    print(f"include_locales={0 if args.no_include_locales else 1}")
    print(f"charset_out={charset_out}")
    print(f"ranges_out={ranges_out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
