"""Validate firmware against the smallest OTA app slot in its built partition table."""
import argparse
from pathlib import Path
import struct


def ota_capacity(table: bytes) -> int:
    sizes = []
    for offset in range(0, len(table) - 31, 32):
        magic, kind, subtype, _, size = struct.unpack_from('<HBBII', table, offset)
        if magic in (0xFFFF, 0xEBEB):
            break
        if magic != 0x50AA:
            raise ValueError('Invalid ESP partition entry')
        if kind == 0 and 0x10 <= subtype <= 0x1F:
            sizes.append(size)
    if not sizes or min(sizes) == 0:
        raise ValueError('No nonempty OTA app slots in partition table')
    return min(sizes)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--firmware', type=Path, required=True)
    parser.add_argument('--partitions', type=Path, required=True)
    args = parser.parse_args()
    capacity = ota_capacity(args.partitions.read_bytes())
    size = args.firmware.stat().st_size
    print(f'{args.firmware}: size={size} OTA capacity={capacity}')
    if not 0 < size <= capacity:
        raise SystemExit('Firmware is empty or exceeds OTA slot size')


if __name__ == '__main__':
    main()
