import re
from pathlib import Path

src = Path('src/ui/widgets/ime/pinyin_data.h')
text = src.read_text(encoding='utf-8')

chars = set()
for line in text.splitlines():
    line = line.strip()
    if not line or line.startswith('#') or line.startswith('/*') or line.startswith('*/'):
        continue
    if '\t' in line:
        _, rest = line.split('\t', 1)
    else:
        parts = line.split(' ', 1)
        if len(parts) < 2:
            continue
        rest = parts[1]
    for token in rest.split(' '):
        token = token.strip()
        if not token:
            continue
        for ch in token:
            if ch.isspace():
                continue
            chars.add(ch)

# Build sorted list by codepoint
chars_list = sorted(chars, key=lambda c: ord(c))

out = Path('tools/pinyin_chars.txt')
out.write_text(''.join(chars_list), encoding='utf-8')
print(f"chars: {len(chars_list)}")
