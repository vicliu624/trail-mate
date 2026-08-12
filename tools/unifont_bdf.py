#!/usr/bin/env python3
"""Read GNU Unifont BDF glyphs without passing through an outline renderer.

GNU Unifont's BDF files define the final 8x16 and 16x16 pixels.  The helpers
in this module deliberately preserve those pixels: they are shared by the
T-Deck Pro firmware font generator and the downloadable locale-pack builder.
"""

from __future__ import annotations

import gzip
from dataclasses import dataclass
from pathlib import Path


NATIVE_PIXEL_SIZE = 16
FONT_ASCENT = 14
FONT_DESCENT = -2


@dataclass(frozen=True)
class BdfGlyph:
    codepoint: int
    advance: int
    width: int
    height: int
    offset_x: int
    offset_y: int
    rows: tuple[int, ...]


def read_bdf(path: Path) -> str:
    if path.suffix == ".gz":
        with gzip.open(path, "rt", encoding="utf-8") as handle:
            return handle.read()
    return path.read_text(encoding="utf-8")


def parse_ranges(values: list[str]) -> set[int]:
    codepoints: set[int] = set()
    for raw_value in values:
        for item in raw_value.split(","):
            token = item.strip()
            if not token:
                continue
            if "-" in token:
                start_text, end_text = token.split("-", 1)
                start = int(start_text, 0)
                end = int(end_text, 0)
                if end < start:
                    raise ValueError(f"invalid range: {token}")
                codepoints.update(range(start, end + 1))
            else:
                codepoints.add(int(token, 0))
    return codepoints


def requested_codepoints(charset_path: Path | None, ranges: list[str], symbols: str) -> set[int]:
    codepoints = parse_ranges(ranges)
    codepoints.update(ord(ch) for ch in symbols)
    if charset_path is not None:
        codepoints.update(
            ord(ch)
            for ch in charset_path.read_text(encoding="utf-8")
            if ch not in "\ufeff\r\n"
        )
    if not codepoints:
        raise ValueError("provide --charset-file, --range, or --symbols")
    if any(codepoint < 0 or codepoint > 0xFFFF for codepoint in codepoints):
        raise ValueError("GNU Unifont BDF supports only BMP codepoints")
    return codepoints


def parse_bdf_glyphs(source: str, wanted: set[int]) -> dict[int, BdfGlyph]:
    glyphs: dict[int, BdfGlyph] = {}
    block: list[str] = []
    in_glyph = False

    for raw_line in source.splitlines():
        line = raw_line.strip()
        if line.startswith("STARTCHAR "):
            block = []
            in_glyph = True
            continue
        if not in_glyph:
            continue
        if line == "ENDCHAR":
            in_glyph = False
            encoding = next((entry.split(" ", 1)[1] for entry in block if entry.startswith("ENCODING ")), None)
            if encoding is None:
                continue
            codepoint = int(encoding)
            if codepoint not in wanted:
                continue
            dwidth = next((entry.split() for entry in block if entry.startswith("DWIDTH ")), None)
            bbox = next((entry.split() for entry in block if entry.startswith("BBX ")), None)
            if dwidth is None or bbox is None:
                raise ValueError(f"incomplete BDF glyph U+{codepoint:04X}")
            bitmap_start = block.index("BITMAP") + 1
            bitmap_rows = block[bitmap_start:]
            width, height, offset_x, offset_y = (int(value) for value in bbox[1:5])
            if len(bitmap_rows) != height:
                raise ValueError(f"invalid bitmap row count for U+{codepoint:04X}")
            glyphs[codepoint] = BdfGlyph(
                codepoint=codepoint,
                advance=int(dwidth[1]),
                width=width,
                height=height,
                offset_x=offset_x,
                offset_y=offset_y,
                rows=tuple(int(row, 16) for row in bitmap_rows),
            )
            continue
        block.append(line)

    missing = sorted(wanted - glyphs.keys())
    if missing:
        preview = ", ".join(f"U+{codepoint:04X}" for codepoint in missing[:12])
        raise ValueError(f"BDF is missing {len(missing)} requested glyph(s): {preview}")
    return glyphs


def ensure_native_grid(glyphs: dict[int, BdfGlyph]) -> None:
    invalid = [
        glyph
        for glyph in glyphs.values()
        if glyph.width not in (8, 16) or glyph.height != NATIVE_PIXEL_SIZE
    ]
    if invalid:
        preview = ", ".join(f"U+{glyph.codepoint:04X}={glyph.width}x{glyph.height}" for glyph in invalid[:8])
        raise ValueError(f"Unifont glyphs must retain their native 8x16/16x16 grid: {preview}")


def row_pixels(glyph: BdfGlyph, row: int) -> list[int]:
    """Return one BDF row as 0/255 opacities in its original left-to-right order."""
    row_bits = glyph.rows[row]
    return [
        255 if (row_bits >> (glyph.width - 1 - column)) & 1 else 0
        for column in range(glyph.width)
    ]


def glyph_bitmap_bytes(glyph: BdfGlyph) -> bytes:
    """Pack a BDF glyph as LVGL 1bpp pixels, retaining all native grid rows."""
    row_bytes = (glyph.width + 7) // 8
    result = bytearray()
    for row in range(glyph.height):
        packed = bytearray(row_bytes)
        for column, pixel in enumerate(row_pixels(glyph, row)):
            if pixel:
                packed[column // 8] |= 1 << (7 - (column % 8))
        result.extend(packed)
    return bytes(result)


def make_lv_font_data(glyphs: dict[int, BdfGlyph], size: int = NATIVE_PIXEL_SIZE) -> dict[str, object]:
    """Create the glyph-data shape consumed by lv_font_conv's binary writer.

    No FreeType source or outline conversion is involved.  Pixel values are
    the BDF source's final one-bit cells represented as LVGL 8-bit opacities.
    """
    ensure_native_grid(glyphs)
    ordered = [glyphs[codepoint] for codepoint in sorted(glyphs)]
    return {
        "ascent": FONT_ASCENT,
        "descent": FONT_DESCENT,
        "typoAscent": FONT_ASCENT,
        "typoDescent": FONT_DESCENT,
        "typoLineGap": 0,
        "size": size,
        "underlinePosition": -2,
        "underlineThickness": 1,
        "glyphs": [
            {
                "code": glyph.codepoint,
                "advanceWidth": glyph.advance,
                "bbox": {
                    "x": glyph.offset_x,
                    "y": glyph.offset_y,
                    "width": glyph.width,
                    "height": glyph.height,
                },
                "kerning": {},
                "pixels": [row_pixels(glyph, row) for row in range(glyph.height)],
            }
            for glyph in ordered
        ],
    }
