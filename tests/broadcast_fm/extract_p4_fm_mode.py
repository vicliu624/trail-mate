"""Extract exact P4 mode-transition methods for native boundary tests."""
from pathlib import Path
import sys

source = Path(sys.argv[1]).read_text(encoding="utf-8")


def extract(needle):
    assert source.count(needle) == 1, needle
    start = source.index(needle)
    brace = source.index("{", start)
    depth, end = 1, brace + 1
    while depth:
        assert end < len(source)
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


Path(sys.argv[2]).write_text("\n".join(extract("    bool " + name + "(") for name in
    ("beginFsk", "prepareFmPacketMode", "restoreFsk")) + "\n", encoding="utf-8")
Path(sys.argv[3]).write_text(extract("bool Lr2021Radio::configureFsk(") + "\n", encoding="utf-8")
