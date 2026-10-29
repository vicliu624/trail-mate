"""Small real OSM PBF for producer integration; no downloaded map data."""
import pathlib
import struct
import sys
import zlib

def number(value):
    result = bytearray()
    while value > 127:
        result.append((value & 127) | 128)
        value >>= 7
    result.append(value)
    return bytes(result)

def scalar(field, value):
    return number(field << 3) + number(value)

def blob(field, value):
    return number((field << 3) | 2) + number(len(value)) + value

def signed(value):
    return value * 2 if value >= 0 else -value * 2 - 1

def node(identity, lat, lon, keys=(), values=()):
    info = scalar(1, 1) + scalar(2, 1577836800) + scalar(3, 1) + scalar(4, 1) + scalar(5, 8) + scalar(6, 1)
    return scalar(1, signed(identity)) + blob(2, b''.join(map(number, keys))) + blob(3, b''.join(map(number, values))) + blob(4, info) + scalar(8, signed(round(lat * 1e7))) + scalar(9, signed(round(lon * 1e7)))

def block(kind, content):
    payload = scalar(2, len(content)) + blob(3, zlib.compress(content))
    header = blob(1, kind.encode()) + scalar(3, len(payload))
    return struct.pack('>I', len(header)) + header + payload

def make(path):
    strings = ['', 'name', 'amenity', 'drinking_water', 'Test Spring 测试泉', 'highway', 'path', 'Test Trail', 'fixture']
    table = b''.join(blob(1, value.encode()) for value in strings)
    group = blob(1, node(1, 25.005, 102.705, [1, 2], [4, 3]))
    group += blob(1, node(2, 25.001, 102.701)) + blob(1, node(3, 25.009, 102.709))
    way = scalar(1, 10) + blob(2, number(5) + number(1)) + blob(3, number(6) + number(7)) + blob(4, scalar(1, 1)) + blob(8, number(signed(2)) + number(signed(1)))
    group += blob(3, way)
    path = pathlib.Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(block('OSMHeader', blob(4, b'OsmSchema-V0.6')) + block('OSMData', blob(1, table) + blob(2, group)))

if __name__ == '__main__':
    make(sys.argv[1])
