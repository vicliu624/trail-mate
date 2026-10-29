"""Independent ARM/formal-app track signing and reconnect verification."""
import hashlib
import pathlib
import secrets
import struct
import sys
import tempfile
import time
from linux_identity_restore_wire_test import Fixture, verify
from linux_track_snapshots_wire_test import Session
from linux_tms_application_test import encode as scalar_encode


def encode(value):
    if isinstance(value, list):
        prefix = bytes([0x90 + len(value)]) if len(value) < 16 else b'\xdc' + struct.pack('>H', len(value))
        return prefix + b''.join(map(encode, value))
    if isinstance(value, str):
        raw = value.encode()
        assert len(raw) <= 255
        return (bytes([0xa0 + len(raw)]) if len(raw) < 32 else b'\xd9' + bytes([len(raw)])) + raw
    return scalar_encode(value)


def upload(client, kind, content):
    identity = secrets.token_bytes(16)
    reply = client.call(32, [identity, kind, len(content), hashlib.sha256(content).digest()])
    assert reply[:3] == [identity, 320, 0]
    for offset in range(0, len(content), 320):
        reply = client.call(33, [identity, offset, content[offset:offset + 320]])
        assert reply[0] == identity
    deadline = time.monotonic() + 10
    while True:
        reply = client.call(35, [identity])
        if reply[3] == 1:
            assert reply[2] == len(content)
            return identity
        assert time.monotonic() < deadline
        time.sleep(.02)


def prepared(client, operation, body):
    deadline = time.monotonic() + 20
    while True:
        result = client.call(operation, body)
        assert len(result) == 6 and result[0] == body[0] and result[1] in (0, 1)
        if result[1] == 1:
            assert len(result[2]) == len(result[3]) == 32 and result[4] == 1 and len(result[5]) == 64
            return result
        assert result[2:] == [None, None, 0, None] and time.monotonic() < deadline
        time.sleep(.02)


def main():
    binary, openssl, image_path = sys.argv[1:4]
    application = sys.argv[4] if len(sys.argv) > 4 else None
    profile = sys.argv[5] if len(sys.argv) > 5 else None
    for version, assets in ((1, False), (2, True), (2, False)):
        with tempfile.TemporaryDirectory(prefix='trailmate-arm-track-signing-') as directory:
            root = pathlib.Path(directory)
            fixture = client = None
            try:
                fixture = Fixture(binary, root, 'seed', application, profile)
                endpoint = fixture.endpoint()
                client = Session(endpoint, secrets.token_bytes(16))
                author = client.call(1, [])
                operation = 96 if version == 1 else 99
                assert operation in client.call(0, [])[4]
                content = b'<kml xmlns="http://www.opengis.net/kml/2.2"><Document><Placemark><description><![CDATA[<img src="photos/example.jpg">]]></description><Point><coordinates>1,2</coordinates></Point></Placemark></Document></kml>'
                if not assets:
                    content = content.replace(b'<description><![CDATA[<img src="photos/example.jpg">]]></description>', b'')
                digest = hashlib.sha256(content).digest()
                image = pathlib.Path(image_path).read_bytes()
                image_hash = hashlib.sha256(image).digest()
                index = encode([1, digest, len(content), [['photo', image_hash, len(image), 'image/jpeg', 'photos/example.jpg', 0, 1]]])
                nonce = secrets.token_bytes(16)
                record = [version, author[0], nonce, 0, 1, None, 0, 100, 100, 'Local trail', '', [10000000, 20000000, 10000000, 20000000], [0, None, None, 1], [digest, len(content), 'application/vnd.google-earth.kml+xml', 1]]
                if version == 2:
                    record.extend([[0, [], 0, 1, 0, 0, 0, None], [hashlib.sha256(index).digest(), len(index), 1, 1] if assets else None])
                raw = encode(record)
                kml_id = upload(client, 3, content)
                manifest_id = upload(client, 4, raw)
                binding = [secrets.token_bytes(16), kml_id, manifest_id, author[3], None]
                if version == 2:
                    mapping = None
                    if assets:
                        index_id = upload(client, 8, index)
                        image_id = upload(client, 9, image)
                        mapping = upload(client, 10, encode([1, index_id, image_id]))
                    binding.append(mapping)
                result = prepared(client, operation, binding)
                assert result[2] == hashlib.sha256(b'TrailMate.Track.ID.v1\0' + author[0] + nonce).digest()
                assert result[3] == hashlib.sha256(f'TrailMate.Track.Revision.v{version}\0'.encode() + raw).digest()
                verify(author[0][32:], result[5], f'TrailMate.Track.Manifest.v{version}\0'.encode() + raw, root, openssl)
                retained = root / 'trailmate/tracks' / result[2].hex()
                files = [path.read_bytes() for path in retained.iterdir()]
                assert content in files
                if assets:
                    assert index in files and image in files
                client.close()
                client = Session(endpoint, secrets.token_bytes(16))
                recovered = prepared(client, operation, binding)
                assert recovered == result
                client.call(operation, [*binding[:3], author[3] + 1, *binding[4:]], 9)
                assert client.call(1, []) == author
                print('TRACK_SIGNING_FORMAL_APP_PASSED' if application else 'TRACK_SIGNING_ARM_WIRE_PASSED', version, 'assets' if assets else 'no-assets', flush=True)
            finally:
                if client:
                    client.close()
                if fixture:
                    fixture.close()


if __name__ == '__main__':
    main()
