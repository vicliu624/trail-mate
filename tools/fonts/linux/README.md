# Linux color emoji source

`TrailMateColorEmoji14.ttf` is an OFL 1.1 derivative of Google's Noto Color Emoji.
Its pinned source, SHA256, generator versions and output SHA256 are recorded in
`font-source.json`. The original license is retained in
`LICENSE-NotoColorEmoji.txt`; copyright records in the font are preserved.

The generator resizes the upstream 109 ppem CBDT PNG strike to 14 ppem using
Lanczos resampling, scales bitmap metrics and renames the derivative. Typical
glyph images are approximately 17 by 16 pixels, suitable for compact text rows.
It retains 1,510 Unicode mappings and 4,004 bitmap glyphs. LVGL's scalar font
fallback can display individual emoji; retaining OpenType sequence tables does
not imply support for every ZWJ, flag or skin-tone sequence.

Regenerate using the exact source URL in the receipt and pinned build tools:

```sh
python3 -m pip install -r tools/fonts/linux/requirements.txt
python3 tools/generate_linux_color_emoji.py --source /path/to/NotoColorEmoji.ttf \
  --output tools/fonts/linux/TrailMateColorEmoji14.ttf \
  --receipt tools/fonts/linux/font-source.json
```

`tools/build_linux_font_pack.py` copies this font, the existing complete Noto CJK
and Latin sources, their licenses and released language tables into a separate
`trailmate-linux-fonts` package. It does not change the ESP binfont assets.
