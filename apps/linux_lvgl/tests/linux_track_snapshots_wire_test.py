"""Independent TCP/catalog/snapshot acceptance on ARM and an installed app."""
import hashlib
import pathlib
import secrets
import sys
import tempfile
import time
from linux_identity_restore_wire_test import Fixture
from linux_tms_application_test import Peer, encode as scalar_encode


def encode(value):
    if isinstance(value, str):
        raw = value.encode('utf-8')
        assert len(raw) < 32
        return bytes([0xa0 + len(raw)]) + raw
    if isinstance(value, list):
        assert len(value) < 16
        return bytes([0x90 + len(value)]) + b''.join(map(encode, value))
    return scalar_encode(value)


def decode(data):
    offset = 0

    def take(size):
        nonlocal offset
        value = data[offset:offset + size]
        assert len(value) == size
        offset += size
        return value

    def item():
        tag = take(1)[0]
        if tag < 128:
            return tag
        if 0x90 <= tag <= 0x9f:
            return [item() for _ in range(tag & 15)]
        if 0xa0 <= tag <= 0xbf:
            return take(tag & 31).decode('utf-8')
        if tag == 0xc0:
            return None
        if tag in (0xcc, 0xcd, 0xce, 0xcf):
            return int.from_bytes(take({0xcc: 1, 0xcd: 2, 0xce: 4, 0xcf: 8}[tag]), 'big')
        if tag in (0xc4, 0xc5, 0xc6, 0xd9, 0xda, 0xdb):
            width = {0xc4: 1, 0xc5: 2, 0xc6: 4, 0xd9: 1, 0xda: 2, 0xdb: 4}[tag]
            raw = take(int.from_bytes(take(width), 'big'))
            return raw.decode('utf-8') if tag >= 0xd9 else raw
        if tag in (0xdc, 0xdd):
            return [item() for _ in range(int.from_bytes(take(2 if tag == 0xdc else 4), 'big'))]
        raise AssertionError(('CMP tag', tag))
    result = item()
    assert offset == len(data)
    return result


class Session(Peer):
    def call(self, operation, body, expected=0):
        request = secrets.token_bytes(16)
        self.send(0x20, encode([1, self.session, request, operation, body]))
        version, session, correlation, status, result = decode(self.receive(0x21))
        assert version == 1 and session == self.session and correlation == request
        assert status == expected, (operation, status, expected)
        return result


def main():
    binary = sys.argv[1]
    application = sys.argv[2] if len(sys.argv) > 2 else None
    profile = sys.argv[3] if len(sys.argv) > 3 else None
    with tempfile.TemporaryDirectory(prefix='trailmate-arm-snapshot-') as temporary:
        root = pathlib.Path(temporary)
        fixture = client = None
        try:
            fixture = Fixture(binary, root, 'seed', application, profile)
            endpoint = fixture.endpoint()
            client = Session(endpoint, secrets.token_bytes(16))
            before = client.call(1, [])
            operations = client.call(0, [])[4]
            assert all(operation in operations for operation in (80, 81, 86, 87, 88))
            (root / 'tracks').mkdir(exist_ok=True)
            for kind, suffix in enumerate(('kml', 'gpx', 'csv', 'bin')):
                filename = f'track.{suffix}'
                source = (f'isolated {suffix} payload\n'.encode() * 60)
                (root / 'tracks' / filename).write_bytes(source)
                cursor = None
                while True:
                    media, cursor, row = client.call(80, [cursor])
                    if row and row[0] == filename:
                        assert row[1:] == [kind, len(source), 0]
                        break
                    assert cursor is not None, 'catalog omitted test recording'
                    cursor = [media, cursor]
                snapshot = secrets.token_bytes(16)
                selection = [snapshot, media, filename, len(source), kind]
                deadline = time.monotonic() + 15
                while True:
                    state = client.call(86, selection)
                    assert state[0] == snapshot and state[3:] == [len(source), kind]
                    if state[1] == 3:
                        assert state[2] == len(source)
                        break
                    assert state[1] == 1 and time.monotonic() < deadline
                    time.sleep(.03)
                (root / 'tracks' / filename).write_bytes(b'changed after seal')
                client.close()
                client = Session(endpoint, secrets.token_bytes(16))
                output, digest = bytearray(), None
                while len(output) < len(source):
                    row = client.call(81, [snapshot, len(output), 320, digest])
                    assert row[0] == snapshot and row[1] == len(source) and row[3] == kind and row[5] == len(output)
                    assert 0 < len(row[4]) <= 320
                    if digest is not None:
                        assert digest == row[2]
                    digest = row[2]
                    output.extend(row[4])
                assert bytes(output) == source and hashlib.sha256(output).digest() == digest
                client.call(81, [snapshot, 0, 320, bytes(32)], 4)
                assert client.call(88, [snapshot]) == []
                assert client.call(88, [snapshot]) == []
                client.call(81, [snapshot, 0, 320, None], 10)
            assert client.call(1, []) == before
            print('TRACK_SNAPSHOTS_FORMAL_APP_PASSED' if application else 'TRACK_SNAPSHOTS_ARM_WIRE_PASSED', flush=True)
        finally:
            if client:
                client.close()
            if fixture:
                fixture.close()


if __name__ == '__main__':
    main()
