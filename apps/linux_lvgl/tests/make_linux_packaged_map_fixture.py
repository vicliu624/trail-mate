"""Add native raster/annotation sections to the pinned SDK POI fixture."""
from pathlib import Path
import struct
import subprocess
import sys

sdk, target = map(Path, sys.argv[1:3])
subprocess.run([sys.executable, str(Path(__file__).with_name("make_linux_search_fixture.py")), str(sdk), str(target), *sys.argv[3:]], check=True)
sys.path[:0] = [str(sdk / "tools"), str(sdk / "tests")]
from make_legacy_administration import page, seal
from tmap_pack import crc32c

source = target.read_bytes()
sections = {}
for index in range(struct.unpack_from("<I", source, 48)[0]):
    kind, version, flags, identity, owner, offset, length, count, root, _, _ = struct.unpack_from("<IHHII6Q", source, 4096 + index * 64)
    sections[identity] = (kind, flags, owner, source[offset:offset + length], count, root)

def tile_key(z, x, y):
    return (z << 58) | sum(((x >> bit) & 1) << (bit * 2) | ((y >> bit) & 1) << (bit * 2 + 1) for bit in range(29))

layers = bytearray()
for layer, semantic, codec in [(1, 1, 1), (2, 2, 1), (3, 3, 2), (4, 104, 2), (5, 114, 2)]:
    locations = [(x, y) for x in range(131070, 131075) for y in range(131070, 131073)] if layer == 1 else [(131072, 131072)]
    locations.sort(key=lambda xy: tile_key(18, *xy))
    table = page(3, len(locations), 32, 4096)
    pixels = bytearray()
    for row, (x, y) in enumerate(locations):
        if codec == 1:
            # Distinct solid native-endian-independent wire pixels.
            color = 0xF800 if layer == 1 else 0x07E0
            payload = struct.pack("<H", color) * (256 * 256)
        else:
            payload = bytearray(256 * 256 * 4)
            for at in range(256 * 256):
                px, py = at % 256, at // 256
                if layer == 3:
                    rgba = (11, 22, 33, 255)
                else:
                    visible = abs((py if layer == 4 else px) - 64) < 3
                    rgba = (0, 0, 255, 128 if visible else 0) if layer == 4 else (0, 255, 0, 128 if visible else 0)
                payload[at * 4:at * 4 + 4] = bytes(rgba)
        struct.pack_into("<QQIIHHI", table, 64 + row * 32, tile_key(18, x, y), len(pixels), len(payload), len(payload), codec, 0, crc32c(payload))
        pixels += payload
    index_id, pixels_id = 100 + layer * 2, 101 + layer * 2
    descriptor = bytearray(96)
    struct.pack_into("<IHHI", descriptor, 0, layer, semantic, 256, 1 << 18)
    struct.pack_into("<2I", descriptor, 16, index_id, pixels_id)
    layers += descriptor
    sections[index_id] = (10, 2, layer, bytes(4096) + seal(table), len(locations), 4096)
    sections[pixels_id] = (11, 0, layer, bytes(pixels), len(locations), 0)
sections[2] = (2, 0, 0, bytes(layers), 5, 0)

# Two typed annotations: a POI and a road with real projected path geometry.
pois = bytearray(sections[20][3])
struct.pack_into("<2i", pois, 64 + 16, -6866, 6866)
sections[20] = (20, 2, 0, seal(pois), 2, 0)
spatial = bytearray(sections[24][3])
struct.pack_into("<4i", spatial, 4096 + 64, 6866, -6866, 6866, -6866)
sections[24] = (24, 2, 0, bytes(4096) + seal(spatial[4096:]), 2, 4096)
annotations = page(15, 2, 40, 4096)
struct.pack_into("<QHBBH2xiiQ8x", annotations, 64, 1, 1, 18, 18, 50000, -6866, 6866, 0)
struct.pack_into("<QHBBH2xiiQ8x", annotations, 104, 2, 3, 18, 18, 60000, -3000, 7000, 64)
geometry = page(16, 1, 0)
struct.pack_into("<HHIII4i", geometry, 64, 1, 0, 16, 2, 0, -3000, 1000, -3000, 12000)
annotation_index = page(3, 1, 32, 4096)
struct.pack_into("<QQI12x", annotation_index, 64, tile_key(18, 131072, 131072), 4096, 80)
sections[40] = (40, 2, 0, bytes(4096) + seal(annotation_index), 1, 4096)
sections[41] = (41, 2, 0, bytes(4096) + seal(annotations), 2, 0)
sections[42] = (42, 2, 0, seal(geometry), 1, 0)

header = bytearray(source[:8192])
directory, body = bytearray(), bytearray()
for identity, (kind, flags, owner, payload, count, root) in sorted(sections.items()):
    offset = 8192 + len(body)
    directory += struct.pack("<IHHII6Q", kind, 1, flags, identity, owner, offset, len(payload), count, root, 0, 0)
    body += payload
    body += bytes((-len(body)) % 4096)
struct.pack_into("<Q", header, 32, 8192 + len(body))
struct.pack_into("<I", header, 48, len(sections))
struct.pack_into("<4i", header, 120, -100000, -100000, 100000, 100000)
struct.pack_into("<I", header, 136, 1 << 18)
struct.pack_into("<I", header, 164, crc32c(directory))
struct.pack_into("<I", header, 160, 0)
struct.pack_into("<I", header, 160, crc32c(header[:256]))
header[4096:8192] = bytes(4096)
header[4096:4096 + len(directory)] = directory
target.write_bytes(header + body)
