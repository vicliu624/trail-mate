"""Derive a compact, reproducible CBDT font from the pinned Noto Color Emoji."""
from __future__ import annotations

import argparse
import hashlib
from io import BytesIO
import json
from pathlib import Path

from fontTools.ttLib import TTFont
from PIL import Image

UPSTREAM_REVISION = "99701e07d3d1fe1bdbcdb14964547f0fd592966f"
UPSTREAM_SHA256 = "15671215ab769fdc7162a045d56fd7d7e477c51b04e6b3c761d914d8fdd6cc44"
UPSTREAM_URL = f"https://raw.githubusercontent.com/googlefonts/noto-emoji/{UPSTREAM_REVISION}/2D/fonts/NotoColorEmoji.ttf"


def generate(source: Path, output: Path) -> dict:
    digest = hashlib.sha256(source.read_bytes()).hexdigest()
    if digest != UPSTREAM_SHA256:
        raise ValueError("Input is not the pinned upstream Noto Color Emoji font")
    font = TTFont(source, recalcTimestamp=False)
    if len(font["CBLC"].strikes) != 1:
        raise ValueError("Expected one upstream bitmap strike")
    strike = font["CBLC"].strikes[0]
    bitmap = strike.bitmapSizeTable
    scale = 14 / bitmap.ppemY
    if bitmap.ppemX != bitmap.ppemY:
        raise ValueError("Expected square upstream ppem")
    bitmap.ppemX = bitmap.ppemY = 14
    for metrics in (bitmap.hori, bitmap.vert):
        for field in ("ascender", "descender", "widthMax", "caretOffset", "minOriginSB",
                      "minAdvanceSB", "maxBeforeBL", "minAfterBL"):
            setattr(metrics, field, round(getattr(metrics, field) * scale))
    count = 0
    for glyph in font["CBDT"].strikeData[0].values():
        if type(glyph).__name__ != "cbdt_bitmap_format_17":
            raise ValueError("Unexpected upstream CBDT image format")
        image = Image.open(BytesIO(glyph.imageData)).convert("RGBA")
        image = image.resize((max(1, round(image.width * scale)),
                              max(1, round(image.height * scale))), Image.Resampling.LANCZOS)
        target = BytesIO()
        image.save(target, format="PNG", optimize=False, compress_level=9)
        glyph.imageData = target.getvalue()
        glyph.metrics.width, glyph.metrics.height = image.size
        for field in ("BearingX", "BearingY", "Advance"):
            setattr(glyph.metrics, field, round(getattr(glyph.metrics, field) * scale))
        count += 1
    names = {1: "TrailMate Color Emoji 14", 2: "Regular", 3: "TrailMateColorEmoji14-1.0",
             4: "TrailMate Color Emoji 14", 6: "TrailMateColorEmoji14"}
    for record in font["name"].names:
        if record.nameID in names:
            record.string = names[record.nameID].encode(record.getEncoding())
    output.parent.mkdir(parents=True, exist_ok=True)
    font.save(output)
    result = {"upstream_url": UPSTREAM_URL, "upstream_revision": UPSTREAM_REVISION,
              "upstream_sha256": digest, "license": "OFL-1.1", "bitmap_ppem": 14,
              "bitmap_glyphs": count, "unicode_codepoints": len(font.getBestCmap()),
              "derivation": "RGBA PNGs resized with Lanczos; bitmap metrics scaled; derivative font renamed",
              "fonttools": __import__("fontTools").__version__, "pillow": Image.__version__,
              "output_sha256": hashlib.sha256(output.read_bytes()).hexdigest(),
              "output_size": output.stat().st_size}
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--receipt", type=Path, required=True)
    args = parser.parse_args()
    result = generate(args.source, args.output)
    args.receipt.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(result, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
