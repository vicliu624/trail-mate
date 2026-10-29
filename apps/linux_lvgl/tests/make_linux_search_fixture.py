"""Extend the SDK's two-POI schema fixture with real name/gram postings.

This is a deliberately tiny test writer, not an application map producer.
Pages, CRCs, POIs, administrative paths and bounds use the pinned SDK fixture.
"""
from pathlib import Path
import struct
import sys

sdk, target = map(Path, sys.argv[1:3])
sys.path[:0] = [str(sdk / "tools"), str(sdk / "tests")]
from make_legacy_administration import page, seal, write_legacy_administration
from tmap_pack import crc32c

write_legacy_administration(target)
original = target.read_bytes()
count = struct.unpack_from("<I", original, 48)[0]
sections = {}
for index in range(count):
    row = original[4096 + index * 64:4096 + (index + 1) * 64]
    kind = struct.unpack_from("<I", row)[0]
    offset, length, rows, root = struct.unpack_from("<4Q", row, 16)
    sections[kind] = (original[offset:offset + length], rows, root)

names = ["West Camp", "East Road"] if "--ascii" in sys.argv[3:] else ["West Camp 起点", "East Camp 终点"]
text = page(10, 4, 0)
documents = page(9, 2, 48)
pois = bytearray(sections[20][0])
spatial = bytearray(sections[24][0])
at = 64
grams = {}
for index, name in enumerate(names):
    references = []
    normalized = name.lower()
    for value in [name, normalized]:
        encoded = value.encode("utf-8")
        references.append(at)
        struct.pack_into("<HH", text, at, len(encoded), 0)
        text[at + 4:at + 4 + len(encoded)] = encoded
        at += 4 + len(encoded)
    identity = index + 1
    longitude = index * 20000
    struct.pack_into("<2i", pois, 64 + index * 96 + 16, 0, longitude)
    struct.pack_into("<4i", spatial, 4096 + 64 + index * 32, longitude, 0, longitude, 0)
    struct.pack_into("<3QI", pois, 64 + index * 96 + 32, identity, identity, 1, 1)
    struct.pack_into("<3Q", documents, 64 + index * 48, identity, *references)
    scalars = list(map(ord, normalized))
    keys = {(1 << 42) | (value << 21) for value in scalars}
    keys.update((2 << 42) | (first << 21) | second for first, second in zip(scalars, scalars[1:]))
    for key in keys:
        grams.setdefault(key, []).append(identity)

posting_data = bytearray(4096)
index_page = page(8, len(grams), 40, 4096)
for slot, (key, identities) in enumerate(sorted(grams.items())):
    offset = len(posting_data)
    directory = page(11, 1, 24, offset)
    data = page(12, len(identities), 8, offset + 4096)
    struct.pack_into("<3Q", directory, 64, identities[0], identities[-1], offset + 4096)
    struct.pack_into("<" + "Q" * len(identities), data, 64, *identities)
    posting_data += seal(directory) + seal(data)
    struct.pack_into("<4QI", index_page, 64 + slot * 40, key, offset, len(identities), 1, 0)

sections[20] = (seal(pois), 2, 0)
sections[24] = (bytes(4096) + seal(spatial[4096:]), 2, 4096)
sections[22] = (seal(documents), 2, 0)
sections[23] = (seal(text), 4, 0)
sections[31] = (bytes(4096) + seal(index_page), len(grams), 4096)
sections[32] = (bytes(posting_data), len(grams), 0)
directory = bytearray()
body = bytearray()
offset = 8192
for kind, (payload, rows, root) in sorted(sections.items()):
    directory += struct.pack("<IHHII6Q", kind, 1, 2 if payload else 0, kind, 0,
                             offset, len(payload), rows, root, 0, 0)
    body += payload
    offset += len(payload)
header = bytearray(original[:8192])
struct.pack_into("<Q", header, 32, offset)
struct.pack_into("<I", header, 136, 1 << 16)
struct.pack_into("<I", header, 164, crc32c(directory))
struct.pack_into("<I", header, 160, 0)
struct.pack_into("<I", header, 160, crc32c(header[:256]))
header[4096:4096 + len(directory)] = directory
target.write_bytes(header + body)
