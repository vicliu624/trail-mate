"""Extract exact IDF foreground admission/cleanup bodies for host concurrency tests."""
from pathlib import Path
import sys

source = Path(sys.argv[1]).read_text(encoding="utf-8")
methods = []
walkie = len(sys.argv) > 3 and sys.argv[3] == "walkie"
tx = len(sys.argv) > 3 and sys.argv[3] == "tx"
rx = len(sys.argv) > 3 and sys.argv[3] == "rx"
declarations = (("bool poll_received_packet(uint8_t* buffer, std::size_t capacity, ReceivedPacket* out_packet)",) if rx else
                ("bool transmit_packet(const uint8_t* data, std::size_t size)",) if tx else
                ("bool tryAcquire(Session* out_session)", "void release(Session* session)") if walkie else
                ("bool acquire()", "void release()"))
for declaration in declarations:
    assert source.count(declaration) == (2 if walkie else 1), declaration
    start = source.index(declaration)
    brace = source.index("{", start)
    depth, end = 1, brace + 1
    while depth:
        assert end < len(source)
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    methods.append(source[start:end])
Path(sys.argv[2]).write_text("\n".join(methods) + "\n", encoding="utf-8")
