#!/usr/bin/env python3
"""Generate a reproducible baseline repertoire for Japanese broadcast text.

This covers JIS X 0208 and Unicode kana/fullwidth forms, not all ARIB additional
symbols or downloadable characters. Font coverage must be checked separately.
"""

import argparse
from pathlib import Path


def broadcast_characters() -> set[str]:
    characters: set[str] = set()
    for row in range(0x21, 0x7F):
        for cell in range(0x21, 0x7F):
            encoded = b"\x1b$B" + bytes((row, cell)) + b"\x1b(B"
            try:
                characters.update(encoded.decode("iso2022_jp", errors="strict"))
            except UnicodeDecodeError:
                continue
    # Include combining kana marks and all fullwidth Latin letters. The ARIB
    # decoder emits fullwidth alphanumeric text even in an English UI locale.
    characters.update(chr(code) for code in range(0x3041, 0x3097))
    characters.update(chr(code) for code in range(0x3099, 0x3100))
    characters.update(chr(code) for code in range(0x30A0, 0x3100))
    characters.update(chr(code) for code in range(0xFF01, 0xFF61))
    characters.update(chr(code) for code in range(0xFF61, 0xFFA0))
    # U+3000 is a real fullwidth broadcast space with an advance width, not a
    # charset-file delimiter. Dropping all Unicode whitespace would lose it.
    return {char for char in characters if char not in "\r\n\t" and ord(char) >= 128}


def write_repertoire(output: Path) -> None:
    points = sorted(ord(char) for char in broadcast_characters())
    output.mkdir(parents=True, exist_ok=True)
    (output / "charset.txt").write_text(
        "".join(chr(point) for point in points) + "\n", encoding="utf-8"
    )
    ranges: list[str] = []
    first = last = points[0]
    for point in points[1:] + [None]:
        if point is not None and point == last + 1:
            last = point
            continue
        ranges.append(f"0x{first:X}" if first == last else f"0x{first:X}-0x{last:X}")
        first = last = point
    (output / "ranges.txt").write_text(
        ",".join(ranges) + "\n", encoding="utf-8"
    )
    print(f"Japanese broadcast baseline: {len(points)} code points -> {output}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    write_repertoire(parser.parse_args().output)
