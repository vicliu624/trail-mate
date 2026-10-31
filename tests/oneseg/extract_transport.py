"""Extract unchanged production FIFO/clock methods for fake-boundary tests."""
from pathlib import Path
import sys

root, output = map(Path, sys.argv[1:])
for filename, signature, destination in (
    ("lr2021_radio.cpp", "    int iqFifoRead(", "fifo.inc"),
    ("linux_radiolib_hal.cpp", "bool LinuxRadioLibHal::setIqTransferMode(", "clock.inc"),
    ("linux_radiolib_hal.cpp", "void LinuxRadioLibHal::spiTransfer(", "transfer.inc"),
    ("lr2021_radio.cpp", "    int configureBoardRfSwitch(", "rf_board.inc"),
    ("lr2021_radio.cpp", "    bool acquire(", "rf_acquire.inc"),
    ("lr2021_radio.cpp", "    bool configureLoRa(", "rf_lora.inc"),
    ("lr2021_radio.cpp", "    bool configureFsk(", "rf_fsk.inc"),
    ("lr2021_radio.cpp", "    int iqReset(", "rf_reset.inc"),
):
    source = (root / "platform/linux/common/src/platform/linux" / filename).read_text(encoding="utf-8")
    assert source.count(signature) == 1
    start = source.index(signature)
    brace = source.index("{", start)
    end, depth = brace + 1, 1
    while depth:
        assert end < len(source)
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    # The isolated harness has no virtual base; bodies stay unchanged.
    method = source[start:end].replace(" override", "", 1)
    (output / destination).write_text(method + "\n", encoding="utf-8")
