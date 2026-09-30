"""Independent SHA-256/HKDF vector for the native 20-round discovery stamp.

Uses only Python's standard library; algorithm follows Reticulum Discovery.py
and LXMF LXStamper.py. Run manually to reproduce the C++ test vector.
"""
import hashlib
import hmac

payload = b"Reticulum discovery conformance"
material = hashlib.sha256(payload).digest()
work = bytearray()
for round_number in range(20):
    salt = hashlib.sha256(material + bytes([round_number])).digest()
    key = hmac.digest(salt, material, "sha256")
    previous = b""
    for block in range(1, 9):
        previous = hmac.digest(key, previous + bytes([block]), "sha256")
        work.extend(previous)

base = hashlib.sha256(work)
for counter in range(10000000):
    stamp = counter.to_bytes(32, "big")
    result = base.copy()
    result.update(stamp)
    if result.digest()[:2] == b"\x00\x00":
        print("stamp:", stamp.hex())
        print("digest:", result.hexdigest())
        break
else:
    raise RuntimeError("No stamp within bounded search")
