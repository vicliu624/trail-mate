"""Extract the production RX method; only remove its class override specifier."""
from pathlib import Path
import sys

source = Path(sys.argv[1]).read_text(encoding="utf-8")
needle = "    bool pollReceive(RadioPacket* packet) override"
assert source.count(needle) == 1
start = source.index(needle)
brace = source.index("{", start)
depth, end = 1, brace + 1
while depth:
    assert end < len(source)
    depth += (source[end] == "{") - (source[end] == "}")
    end += 1
method = source[start:end].replace(" override", "", 1)
Path(sys.argv[2]).write_text(method + "\n", encoding="utf-8")
