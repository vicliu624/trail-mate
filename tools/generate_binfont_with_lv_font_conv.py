#!/usr/bin/env python3

from __future__ import annotations

import argparse
import json
import shutil
import subprocess
import sys
import tarfile
import tempfile
from pathlib import Path


NODE_DRIVER = r"""
const fs = require('fs');
const path = require('path');

async function main() {
  const configPath = process.argv[2];
  const config = JSON.parse(fs.readFileSync(configPath, 'utf8'));
  const convert = require(path.resolve(config.package_dir, 'lib', 'convert'));
  const charset = fs.readFileSync(config.charset_file, 'utf8').replace(/\r/g, '').replace(/\n/g, '');

  const args = {
    size: config.size,
    bpp: config.bpp,
    format: 'bin',
    output: config.output,
    no_compress: config.no_compress,
    no_prefilter: config.no_prefilter,
    no_kerning: config.no_kerning,
    fast_kerning: false,
    lcd: false,
    lcd_v: false,
    use_color_info: false,
    font: [
      {
        source_path: config.font,
        source_bin: fs.readFileSync(config.font),
        ranges: [{ symbols: charset }]
      }
    ]
  };

  const files = await convert(args);
  for (const [outputPath, data] of Object.entries(files)) {
    fs.mkdirSync(path.dirname(outputPath), { recursive: true });
    fs.writeFileSync(outputPath, Buffer.isBuffer(data) ? data : Buffer.from(data));
  }
}

main().catch((error) => {
  console.error(error && error.stack ? error.stack : String(error));
  process.exit(1);
});
"""


def resolve_executable(candidate: str, fallback: str | None = None) -> str:
    path_candidate = Path(candidate)
    if path_candidate.exists():
        return str(path_candidate.resolve())

    discovered = shutil.which(candidate)
    if discovered:
        return discovered

    if fallback:
        fallback_path = Path(fallback)
        if fallback_path.exists():
            return str(fallback_path.resolve())
        discovered = shutil.which(fallback)
        if discovered:
            return discovered

    raise FileNotFoundError(f"unable to resolve executable: {candidate}")


def infer_npm_from_node(node_exe: str) -> str | None:
    node_path = Path(node_exe)
    sibling = node_path.with_name("npm.cmd")
    if sibling.exists():
        return str(sibling.resolve())
    sibling = node_path.with_name("npm")
    if sibling.exists():
        return str(sibling.resolve())
    return None


def main() -> int:
    parser = argparse.ArgumentParser(description="Generate an LVGL binary font from a charset file via lv_font_conv.")
    parser.add_argument("--font", type=Path, required=True, help="Source font file, for example tools/fonts/NotoSansCJKsc-Regular.otf")
    parser.add_argument("--charset-file", type=Path, required=True, help="UTF-8 charset.txt file listing glyphs to include")
    parser.add_argument("--output", type=Path, required=True, help="Output font.bin path")
    parser.add_argument("--size", type=int, default=16, help="Font size in pixels")
    parser.add_argument("--bpp", type=int, default=2, help="Bits per pixel")
    parser.add_argument("--node-exe", default="node", help="Node.js executable to use")
    parser.add_argument("--npm-exe", default=None, help="npm executable to use; defaults to npm next to --node-exe when possible")
    parser.add_argument("--package-spec", default="lv_font_conv", help="npm package spec to pack, defaults to lv_font_conv")
    parser.add_argument("--no-compress", action="store_true", help="Disable lv_font_conv RLE compression")
    parser.add_argument("--no-prefilter", action="store_true", help="Disable lv_font_conv XOR prefilter")
    parser.add_argument("--no-kerning", action="store_true", help="Disable kerning generation")
    args = parser.parse_args()

    node_exe = resolve_executable(args.node_exe)
    npm_hint = args.npm_exe or infer_npm_from_node(node_exe) or "npm"
    npm_exe = resolve_executable(npm_hint)

    font_path = args.font.resolve()
    charset_path = args.charset_file.resolve()
    output_path = args.output.resolve()

    if not font_path.exists():
        raise FileNotFoundError(f"font file not found: {font_path}")
    if not charset_path.exists():
        raise FileNotFoundError(f"charset file not found: {charset_path}")

    with tempfile.TemporaryDirectory(prefix="lv-font-conv-") as temp_dir_name:
        temp_dir = Path(temp_dir_name)
        subprocess.run(
            [npm_exe, "pack", args.package_spec],
            cwd=temp_dir,
            check=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
        )

        tarballs = sorted(temp_dir.glob("*.tgz"))
        if not tarballs:
            raise FileNotFoundError("npm pack did not produce a tarball")

        with tarfile.open(tarballs[0], "r:gz") as archive:
            extract_kwargs = {"filter": "data"} if sys.version_info >= (3, 12) else {}
            archive.extractall(temp_dir, **extract_kwargs)

        package_dir = temp_dir / "package"
        if not package_dir.is_dir():
            raise FileNotFoundError(f"expected extracted package directory not found: {package_dir}")

        config_path = temp_dir / "config.json"
        config_path.write_text(
            json.dumps(
                {
                    "package_dir": str(package_dir),
                    "font": str(font_path),
                    "charset_file": str(charset_path),
                    "output": str(output_path),
                    "size": args.size,
                    "bpp": args.bpp,
                    "no_compress": args.no_compress,
                    "no_prefilter": args.no_prefilter,
                    "no_kerning": args.no_kerning,
                },
                indent=2,
            ),
            encoding="utf-8",
        )

        driver_path = temp_dir / "driver.js"
        driver_path.write_text(NODE_DRIVER, encoding="utf-8")
        subprocess.run([node_exe, str(driver_path), str(config_path)], cwd=temp_dir, check=True)

    print(f"font={font_path}")
    print(f"charset_file={charset_path}")
    print(f"output={output_path}")
    print(f"size={args.size}")
    print(f"bpp={args.bpp}")
    print(f"node_exe={node_exe}")
    print(f"npm_exe={npm_exe}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
