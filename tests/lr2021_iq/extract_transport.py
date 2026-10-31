"""Extract production method bodies for an isolated P4 transport boundary test."""
from pathlib import Path
import sys

source = Path(sys.argv[1]).read_text(encoding="utf-8")
methods = []
names = ["iqRead", "iqWrite", "iqRead32", "iqWrite32", "iqSnapshot", "iqReset", "iqRestore"]
names.extend(sys.argv[3:])
if "    int configureBoardRfSwitch(" in source:
    names.append("configureBoardRfSwitch")
for name in names:
    needle = f"    int {name}("
    assert source.count(needle) == 1, name
    start = source.index(needle)
    brace = source.index("{", start)
    depth = 1
    end = brace + 1
    while depth:
        assert end < len(source), name
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    methods.append(source[start:end])
Path(sys.argv[2]).write_text("\n".join(methods) + "\n", encoding="utf-8")
