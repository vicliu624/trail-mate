"""Check native AutoInterface IFAC and ESP first-pass capability rejection."""
import struct
import subprocess
import sys

esp, native = sys.argv[1:]
source = subprocess.check_output([esp, "--emit"], stderr=subprocess.PIPE)
extension = (b"rt.net.interface.1.network_name=str:private-lan\n"
             b"rt.net.interface.1.passphrase=str:test-secret\n"
             b"rt.net.interface.1.ifac_size=u16:128\n")
assert source.count(b"END\n") == 1
protected = source.replace(b"END\n", extension + b"END\n")
roundtrip = subprocess.check_output([native, "--roundtrip"], input=protected, stderr=subprocess.PIPE)
assert extension in roundtrip


def status(document):
    response = subprocess.check_output([esp, "--batch"], input=struct.pack("<I", len(document)) + document, stderr=subprocess.PIPE)
    assert len(response) >= 5
    size, = struct.unpack("<I", response[1:5])
    assert size == len(response) - 5
    return response[0]


assert status(source) == 0
assert status(roundtrip) == 1  # Validation failure, never the mutating second pass.
print("Native AutoInterface IFAC roundtrip and ESP validation-only rejection passed")
