# Japanese broadcast content font (in development)

This separate content font is intended for broadcast station names and programme
titles on devices with an extended memory profile. It does not enlarge the
shared Japanese UI font used on smaller devices.

Generate the baseline repertoire from Python's strict JIS X 0208 codec mapping:

```sh
python tools/build_japanese_broadcast_charset.py --output packs/ja-broadcast/fonts/ja-broadcast-cjk
```

The repertoire also includes Unicode kana and fullwidth alphanumeric forms
produced by the ARIB text decoder. It is a baseline, not a claim of complete
ARIB STD-B24 additional-symbol or downloadable-character support. Missing font
glyphs must be detected with the native LVGL font coverage test before publishing
a font manifest or including this pack in a device package.

The build recipe expects the Japanese Noto Sans CJK Regular source, licensed
under the SIL Open Font License 1.1. The pinned upstream URL and SHA256 are in
`fonts/ja-broadcast-cjk/font-source.json`; the upstream license is included in
`fonts/ja-broadcast-cjk/LICENSE-Noto.txt`.
Place that verified source at `tools/fonts/NotoSansCJKjp-Regular.otf`, then run:

```sh
python tools/generate_binfont_with_lv_font_conv.py \
  --font tools/fonts/NotoSansCJKjp-Regular.otf \
  --fallback-font tools/fonts/NotoSansMath-Regular.ttf \
  --charset-file packs/ja-broadcast/fonts/ja-broadcast-cjk/charset.txt \
  --output packs/ja-broadcast/fonts/ja-broadcast-cjk/font.bin \
  --size 16 --bpp 2 --no-compress --no-kerning \
  --package-spec lv_font_conv@1.5.3
```

The generated binary is a build artifact. With the Linux UI's OneSeg media
support enabled, build `trailmate_broadcast_font_test` and run it with the binary
and charset paths as its two arguments. This uses the real LVGL binary font
loader and requires every advertised character to resolve without placeholders.

Noto Sans CJK JP lacks U+2252, which is part of the baseline JIS repertoire.
Noto Sans Math supplies missing symbols only; its pinned source and license are
in `fonts/ja-broadcast-cjk/math-font-source.json` and
`fonts/ja-broadcast-cjk/LICENSE-NotoMath.txt`. The converter keeps the
primary Japanese glyphs whenever available.

Native 64-bit Linux LVGL verification checked all 6,960 advertised glyphs,
including the fullwidth broadcast space U+3000. The committed charset is generated
by the dedicated broadcast tool; the package builder must preserve it instead
of applying the UI-locale whitespace filter.

The measured retained LVGL payload was 472,132 bytes in 10 allocations; loading
peaked at 499,980 bytes. The manifest reserves 655,360 bytes to leave room beyond
the measured payload. These figures exclude allocator metadata and libc I/O;
they are not an ARM device measurement. The probe also checks that destroying
the font releases its tracked allocations.

The package is restricted to the extended memory profile. Installation and
automatic application packaging still need qualification before device rollout.
