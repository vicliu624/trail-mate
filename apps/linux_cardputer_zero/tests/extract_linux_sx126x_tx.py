"""Compile production TX/RX methods against a command-state radio fake."""
from pathlib import Path
import sys

source = Path(sys.argv[1]).read_text(encoding="utf-8")
methods = []
for needle in (
    "bool Sx126xRadio::transmit(const std::uint8_t* data, std::size_t size)",
    "bool Sx126xRadio::pollReceive(Sx126xPacket* out)",
):
    assert source.count(needle) == 1
    start = source.index(needle)
    brace = source.index("{", start)
    depth, end = 1, brace + 1
    while depth:
        assert end < len(source)
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    methods.append(source[start:end].replace("Sx126xRadio::", "", 1))
Path(sys.argv[2]).write_text("\n".join(methods) + "\n", encoding="utf-8")
