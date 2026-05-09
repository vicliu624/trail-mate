# Arabic Locale Pack

Arabic language support for Trail Mate, built around Noto Naskh Arabic.

## Features
- Arabic UI and content rendering (RTL)
- Naskh font covering Arabic, Arabic Supplement, and Presentation Forms A/B
- Display-only review locale; Arabic input is not implemented yet
- Arabic translations (strings.tsv)

## Font Strategy
Arabic script requires contextual glyph shaping. Since LVGL binfont does not
perform shaping at runtime, the presentation forms (isolated, final, medial,
initial) are pre-baked into the font at build time. The font covers:
- U+0600–U+06FF  Arabic core
- U+0750–U+077F  Arabic Supplement
- U+FB50–U+FDFF  Presentation Forms-A
- U+FE70–U+FEFF  Presentation Forms-B
- U+0020–U+007F  Basic Latin (for mixed UI)

## Dependencies
Place `NotoNaskhArabic-Regular.otf` in `tools/fonts/` before building the pack.
